/**
 * @file main.cpp
 * @author Nq139 (fnq409997@gmail.com)
 * @brief LPSS 网络监控工具
 * @copyright Copyright 2026, Nq139
 */


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <future>
#include <thread>
#include <atomic>
#include <mutex>
#include <array>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <set>

#include <rmvl/lpss.hpp>
#include <rmvl/io/socket.hpp>

using namespace rm;
using namespace rm::lpss;
using namespace std::chrono_literals;


struct EndpointInfo
{
    std::string topic;
    bool is_pub;
};

/**
 * @brief 全局监控状态
 */
struct MonitorState
{
    std::mutex mtx;
    std::unordered_map<uint64_t, std::string> nodes;
    std::unordered_map<uint64_t, std::vector<EndpointInfo>> topics;
    std::atomic<bool> running{true};
};


inline uint64_t get_prefix(const Guid &g) { return g.full & 0xFFFFFFFFFFFFULL; }

/**
 * @brief Get the local ip object
 * @return std::array<uint8_t, 4>
 */
std::array<uint8_t, 4> get_local_ip()
{
    std::array<uint8_t, 4> res = {0};
    struct ifaddrs *ifa;
    getifaddrs(&ifa);
    for (auto *p = ifa; p; p = p->ifa_next)
    {
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && strcmp(p->ifa_name, "lo") != 0)
        {
            memcpy(res.data(), &((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr, 4);
            break;
        }
    }
    if (ifa)
        freeifaddrs(ifa);
    return res;
}


/**
 * @brief 持续监听 RNDP 报文，收集网络中节点的信息
 */
void task_nodes(MonitorState *state)
{
    auto sock = rm::Listener(rm::Endpoint(rm::ip::udp::v4(), 7500)).create();
    sock.setOption(rm::ip::multicast::JoinGroup(BROADCAST_IP));
    while (state->running)
    {
        auto [data, addr, port] = sock.read(); // 持续监听
        if (data.size() >= 14 && data[0] == 'N')
        {
            auto msg = RNDPMessage::deserialize(data.data());
            std::lock_guard<std::mutex> lock(state->mtx);
            state->nodes[get_prefix(msg.guid)] = msg.name;
        }
    }
}

/**
 * @brief 持续监听 REDP 报文，收集网络中节点的发布/订阅话题信息
 */
void task_topics(MonitorState *state, rm::DgramSocket &&sock)
{
    while (state->running)
    {
        auto [data, addr, port] = sock.read();
        if (data.size() >= 14 && data[0] == 'E')
        {
            auto msg = REDPMessage::deserialize(data.data());
            std::lock_guard<std::mutex> lock(state->mtx);
            auto &list = state->topics[get_prefix(msg.endpoint_guid)];
            bool is_pub = (msg.type == REDPMessage::Type::Writer);
            bool exists = false;
            for (auto &ep : list)
                if (ep.topic == msg.topic && ep.is_pub == is_pub)
                    exists = true;
            if (!exists)
                list.push_back({msg.topic, is_pub});
        }
    }
}


/**
 * @brief 定期广播 RNDP 心跳，诱导网络中的 LPSS 节点回应其存在
 */
void task_heartbeat(MonitorState *state, Guid my_guid, uint16_t port, std::array<uint8_t, 4> ip)
{
    auto sender = rm::Sender(rm::ip::udp::v4()).create();
    while (state->running)
    {
        RNDPMessage msg;
        msg.guid = my_guid;
        msg.name = "lpss_inspector";
        msg.locators.push_back({port, ip});
        sender.write(BROADCAST_IP, rm::Endpoint(rm::ip::udp::v4(), 7500), msg.serialize());
        std::this_thread::sleep_for(1s);
    }
}


/**
 * @brief 生成图形化的网络拓扑结构
 * @param state 全局状态对象
 */
void generate_graph(MonitorState &state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    FILE *fp = fopen("lpss_graph.dot", "w");
    if (!fp)
        return;

    fprintf(fp, "digraph G {\n");
    fprintf(fp, "  rankdir=LR;\n");
    fprintf(fp, "  node [fontname=\"sans-serif\", fontsize=10];\n\n");

    // topic (椭圆节点)
    std::set<std::string> all_topics;
    for (auto &pair : state.topics)
    {
        for (auto &ep : pair.second)
        {
            all_topics.insert(ep.topic);
        }
    }

    for (const auto &t : all_topics)
    {
        fprintf(fp, "  \"t_%s\" [label=\"%s\", shape=ellipse, style=filled, fillcolor=lightyellow];\n",
                t.c_str(), t.c_str());
    }

    // 2. 绘制节点及连线
    for (auto &[prefix, name] : state.nodes)
    {
        // Node ：蓝色方框
        fprintf(fp, "  n%lx [label=\"%s\", shape=box, style=filled, fillcolor=lightblue];\n",
                prefix, name.c_str());

        // 建立连接
        if (state.topics.count(prefix))
        {
            for (auto &ep : state.topics[prefix])
            {
                if (ep.is_pub)
                {
                    // 发布者：节点 -> 话题 (蓝色箭头)
                    fprintf(fp, "  n%lx -> \"t_%s\" [color=blue, label=\"pub\"];\n", prefix, ep.topic.c_str());
                }
                else
                {
                    // 订阅者：话题 -> 节点 (绿色箭头)
                    fprintf(fp, "  \"t_%s\" -> n%lx [color=darkgreen, label=\"sub\"];\n", ep.topic.c_str(), prefix);
                }
            }
        }
    }

    fprintf(fp, "}\n");
    fclose(fp);

    system("dot -Tpng lpss_graph.dot -o lpss_graph.png && xdg-open lpss_graph.png > /dev/null 2>&1 &");///打开图片
}

int main()
{
    MonitorState state;         
    auto my_ip = get_local_ip();
    Guid my_guid;
    my_guid.full = 0x12345678; 

    auto unicast_sock = rm::Listener(rm::Endpoint(rm::ip::udp::v4(), rm::Endpoint::ANY_PORT)).create(); /// 创建 REDP 监听 Socket
    uint16_t my_port = unicast_sock.endpoint().port();                                                  /// 获取分配的端口号

    
    auto fut_a = std::async(std::launch::async, task_nodes, &state);/// 启动节点监听任务                         
    auto fut_b = std::async(std::launch::async, task_topics, &state, std::move(unicast_sock));/// 启动话题监听任务    
    auto fut_c = std::async(std::launch::async, task_heartbeat, &state, my_guid, my_port, my_ip);/// 启动心跳广播任务 
    printf("LPSS Async Monitor running. Commands: list, info <name>, graph, quit\n");

    /**
     * @brief 命令行交互界面
     */
    char buf[256], cmd[64], arg[64];
    while (true)
    {
        printf("> ");
        if (!fgets(buf, sizeof(buf), stdin))
            break;
        int n = sscanf(buf, "%s %s", cmd, arg);
        if (n <= 0)
            continue;

        if (!strcmp(cmd, "list"))
        {
            std::lock_guard<std::mutex> lock(state.mtx);
            for (auto &[p, name] : state.nodes)
                printf("- %s\n", name.c_str());
        }
        else if (!strcmp(cmd, "info") && n == 2)
        {
            std::lock_guard<std::mutex> lock(state.mtx);
            for (auto &[p, name] : state.nodes)
            {
                if (name == arg)
                {
                    for (auto &ep : state.topics[p])
                        printf("  [%s] %s\n", ep.is_pub ? "PUB" : "SUB", ep.topic.c_str());
                }
            }
        }
        else if (!strcmp(cmd, "graph"))
            generate_graph(state);
        else if (!strcmp(cmd, "quit"))
            break;
    }

    state.running = false;
    printf("Shutting down... (Waiting for final packets to unblock threads)\n");
    return 0;
}