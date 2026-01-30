#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <rmvl/lpss.hpp>

using namespace std::chrono_literals;

struct StringMsg
{
    std::string data;
    static constexpr const char *msg_type = "StringMsg";
    std::string serialize() const { return data; }
    static StringMsg deserialize(const std::string &bin) { return {bin}; }
};

int main()
{
    // 1. 创建节点
    rm::lpss::Node node("camera_node");

    std::cout << "[Node Created] Name: camera_node, GUID: 0x"
              << std::hex << node.guid().full << std::dec << std::endl;

    // 2. 创建发布者
    auto pub = node.createPublisher<StringMsg>("message_topic");

    std::cout << "[Publisher] Start publishing..." << std::endl;

    int count = 0;
    while (true)
    {
        StringMsg msg;
        msg.data = "Hello LPSS " + std::to_string(count++);

        pub.publish(msg);

        std::cout << "[Sent] " << msg.data << std::endl;
        std::this_thread::sleep_for(500ms);

        return 0;
    }