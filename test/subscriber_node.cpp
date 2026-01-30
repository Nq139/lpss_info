#include <iostream>
#include <thread>
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
    rm::lpss::Node node("detector_node");

    // 2. 创建订阅者
    node.createSubscriber<StringMsg>("message_topic", [](const StringMsg &msg)
                                     { std::cout << "[Received] " << msg.data << std::endl; });

    std::cout << "[Subscriber] Listening..." << std::endl;

    while (true)
    {
        std::this_thread::sleep_for(1s);
    }

    return 0;
}