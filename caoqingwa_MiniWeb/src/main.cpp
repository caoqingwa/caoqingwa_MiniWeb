#include "event_loop.h"
#include <iostream>

#define SERVER_IP "0.0.0.0"
#define SERVER_PORT 8080


int main() {
    EventLoop* loop = create_event_loop();
	std::cout << "server starting..." << std::endl; 
    loop->init(SERVER_PORT);
    std::cout << "server running at " << SERVER_IP << ":" << SERVER_PORT << std::endl;
    loop->loop();
    delete loop;
    return 0;
}