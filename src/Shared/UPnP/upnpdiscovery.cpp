/* ***********************************************************************
 * Andama
 * (C) 2017 by Yiannis Bourkelis (hello@andama.org)
 *
 * This file is part of Andama.
 *
 * Andama is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Andama is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Andama.  If not, see <http://www.gnu.org/licenses/>.
 * ***********************************************************************/

#include "upnpdiscovery.h"
#include <stdexcept>
#include <algorithm>


#ifdef WIN32
#define NOMINMAX
#include <stdio.h>
#include "winsock2.h"
#include <ws2tcpip.h>
#define bzero(b,len) (memset((b), '\0', (len)), (void) 0)
#define bcopy(b1,b2,len) (memmove((b2), (b1), (len)), (void) 0)
#define in_addr_t uint32_t
#pragma comment(lib, "Ws2_32.lib")

#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

#include <iostream>
#include <thread>
#include <chrono>


UPnPDiscovery::UPnPDiscovery()
{

}

QUrl UPnPDiscovery::getDeviceLocationXmlUrl()
{
    int loop = 1; // Needs to be on to get replies from clients on the same host
    int ttl = 4;

#ifdef WIN32
    SOCKET sock;
#else
    int sock;
#endif

#ifdef WIN32
    // Initialize Winsock
    int iResult;
    WSADATA wsaData;
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        std::cout << "WSAStartup failed: " << iResult << std::endl;
        return QUrl();
        //throw std::runtime_error("WSAStartup failed");
    }
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return QUrl();
        //throw std::runtime_error("socket");
    }

    // Multicast message will be sent to 239.255.255.250:1900
    struct sockaddr_in destadd;
    memset(&destadd, 0, sizeof(destadd));
    destadd.sin_family = AF_INET;
    destadd.sin_port = htons(1900);
    if (inet_pton(AF_INET, "239.255.255.250", &destadd.sin_addr) < 1) {
        perror("inet_pton dest");
        return QUrl();
        //throw std::runtime_error("inet_pton dest");
    }

    // Listen on all interfaces on port MULTICAST_DISCOVERY_BIND_PORT.
    //PROSOXI!!! paratirisa oti sta windows 7 gia kapoio logo den almvanw apantisi
    //sto discovery request, an i porta einai mikroteri apo 12000
    struct sockaddr_in interface_addr;
    memset(&interface_addr, 0, sizeof(interface_addr));
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_port = htons(MULTICAST_DISCOVERY_BIND_PORT);
    interface_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Got to have this to get replies from clients on same machine
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char*) &loop, sizeof(loop)) < 0){
        perror("setsockopt loop");
        return QUrl();
        //throw std::runtime_error("setsockopt loop");
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,(char*) &ttl, sizeof(ttl)) < 0){
        perror("setsockopt ttl");
        return QUrl();
        //throw std::runtime_error("setsockopt ttl");
    }


    //Xreiazetai wste se periptwsi crash na ginetai reuse to socket pou einai se state CLOSE_WAIT
    //mporw na to vrw se macos me: netstat -anp tcp | grep port_number
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

#ifdef SO_REUSEPORT
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEPORT) failed");
#endif


    // Bind to port MULTICAST_DISCOVERY_BIND_PORT on all interfaces
    unsigned short bindPortBindIncrementer = 0;
    while (bind(sock, (struct sockaddr *)&interface_addr,
        sizeof(struct sockaddr_in)) < 0 && bindPortBindIncrementer < 100) {
        perror("bind");
        bindPortBindIncrementer++;
        interface_addr.sin_port = htons(MULTICAST_DISCOVERY_BIND_PORT + bindPortBindIncrementer);
        //std::this_thread::sleep_for(std::chrono::milliseconds(10));//TODO: isws den xreiazetai
    }

    char discovery_request_buffer[1024];
    strcpy(discovery_request_buffer,   "M-SEARCH * HTTP/1.1\r\n"
                                       "Host: 239.255.255.250:1900\r\n"
                                       "Man: \"ssdp:discover\"\r\n"
                                       "ST: upnp:rootdevice\r\n"
                                       "MX: 3\r\n"
                                       "\r\n");
    int disc_send_res = sendto(sock, discovery_request_buffer, strlen(discovery_request_buffer), 0, (struct sockaddr*)&destadd,
               sizeof(destadd));
    if (disc_send_res < 0) {
        perror("sendto");
        return QUrl();
        //throw std::runtime_error("sendto");
    } else if (disc_send_res != strlen(discovery_request_buffer)){
        perror("sendto - send bytes not equal to buffer");
        return QUrl();
    }

    char discovery_response_buffer[1024];
    if (recvfrom(sock, discovery_response_buffer, sizeof(discovery_response_buffer)-1, 0, NULL, NULL) < 0) { //TODO: edw na valw recv timeout
        perror("recvfrom");
        return QUrl();
        //throw std::runtime_error("recvfrom");
    }

#ifdef WIN32
    if (closesocket(sock) < 0) {
#else
    if (close(sock) < 0) {
#endif
        perror("close");
        return QUrl();
        //throw std::runtime_error("close");
    }

    printf("%s\n", discovery_response_buffer);

    //eksagw to discovery url
    //prwta metatrepw to response se lower case
    //giati to location mporei na einai Location, LOCATION klp
    std::string ret(discovery_response_buffer);
    std::transform(ret.begin(), ret.end(), ret.begin(), ::tolower);
    size_t locfinf = ret.find("location:");
    QUrl locationUrl;

    if (locfinf < ret.length()){
        //efoson to location vrethike, anazitw to url
        //sto original response (xwris lowercase diladi), gia na min epireastei to url
        std::string original_ret(discovery_response_buffer);
        //dimiourgw neo string meta to location:
        std::string loc(original_ret.substr(locfinf));
        auto f = loc.find("\r\n");
        std::string loc2(loc.substr(9,f-9));
        std::cout << loc2 << std::endl;
        locationUrl = QString::fromStdString(loc2);
        std::cout << "\r\n" << "Host: " << locationUrl.host().toStdString()<<"\r\nPort: "<< locationUrl.port() << std::endl;
    }

    return locationUrl;
}