//
// Created by Phillip Romig on 7/16/24.
//
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <array>
#include <deque>

#include "timerC.h"
#include "unreliableTransport.h"
#include "logging.h"

#define DEFAULT_WINDOW_SIZE 10
#define DEFAULT_TIMEOUT 1000

int main(int argc, char* argv[]) {
    // Defaults
    uint16_t portNum(12345);
    std::string hostname("");
    std::string inputFilename("");
    u_int windowSize(DEFAULT_WINDOW_SIZE);
    int timeoutThreshold(DEFAULT_TIMEOUT);
    int requiredArgumentCount(0);

    int opt;
    try {
        while ((opt = getopt(argc, argv, "f:h:p:d:")) != -1) {
            switch (opt) {
                case 'p':
                    portNum = std::stoi(optarg);
                    break;
                case 'h':
                    hostname = optarg;
		            requiredArgumentCount++;
                    break;
                case 'd':
                    LOG_LEVEL = std::stoi(optarg);
                    break;
                case 'f':
                    inputFilename = optarg;
		            requiredArgumentCount++;
                    break;
                case 'w':
                    windowSize = std::stoi(optarg);
		            requiredArgumentCount++;
                    break;
                case 't':
                    timeoutThreshold = std::stoi(optarg);
		            requiredArgumentCount++;
                    break;
                case '?':
                default:
                    std::cout << "Usage: " << argv[0] << " -f filename -h hostname [-p port] [-d debug_level]" << std::endl;
                    break;
            }
        }
    } catch (std::exception &e) {
        std::cout << "Usage: " << argv[0] << " -f filename -h hostname [-p port] [-d debug_level]" << std::endl;
        FATAL << "Invalid command line arguments: " << e.what() << ENDL;
        return(-1);
    }

    if (requiredArgumentCount != 2) {
        std::cout << "Usage: " << argv[0] << " -f filename -h hostname [-p port] [-d debug_level]" << std::endl;
        std::cerr << "hostname and filename are required." << std::endl;
        return(-1);
    }

    TRACE << "Command line arguments parsed." << ENDL;
    TRACE << "\tServername: " << hostname << ENDL;
    TRACE << "\tPort number: " << portNum << ENDL;
    TRACE << "\tDebug Level: " << LOG_LEVEL << ENDL;
    TRACE << "\tOutput file name: " << inputFilename << ENDL;
    TRACE << "\tWindow Size: " << windowSize << ENDL;
    TRACE << "\tTimeout Threshold (ms): " << timeoutThreshold << ENDL;

    // *********************************
    // * Open the input file
    // *********************************
    std::ifstream inputFile(inputFilename, std::ios::binary);
    if (!inputFile.is_open()) {
        FATAL << "Unable to read input file" << ENDL;
        return(-1);
    }

    try {

        // ***************************************************************
        // * Initialize your timer, window and the unreliableTransport etc.
        // **************************************************************
        unreliableTransportC udt(hostname, portNum);
        timerC timer;
        // Setting up window:
        std::deque<datagramS> window;
        int nextSeq = 1;

        // ***************************************************************
        // * Send the file one datagram at a time until they have all been
        // * acknowledged
        // **************************************************************
        bool allSent(false);
        bool allAcked(false);
        while ((!allSent) || (!allAcked)) {
		    // Is there space in the window? If so, read some data from the file and send it.
            while ((window.size() < windowSize) && !allSent) {
                datagramS datagram;
                datagram.seqNum = nextSeq;
                // Don't need to worry about setting ACK number since we're not receiving.

                if (!inputFile.read(datagram.data, MAX_PAYLOAD_LENGTH)) {
                    if (inputFile.gcount() > 0) {
                        // Handling if file has less than MAX_PAYLOAD_LENGTH bytes left.
                        datagram.payloadLength = inputFile.gcount();
                        inputFile.read(datagram.data, inputFile.gcount());
                    } else {
                        // Sending empty packet to signal EOF to server.
                        datagram.payloadLength = 0;
                        allSent = true;
                    }
                }
                datagram.checksum = computeChecksum(datagram);
                udt.udt_send(datagram);
                window.push_back(datagram);
                nextSeq++;
            }

            timer.setDuration(timeoutThreshold);
            timer.start();

            // Call udt_recieve() to see if there is an acknowledgment.  If there is, process it.
            datagramS recvDatagram;
            while (true) {
                DEBUG << "\tSEQ: " << nextSeq << ENDL;
                // If packet recieved and valid:
                if (udt.udt_receive(recvDatagram) && validateChecksum(recvDatagram)) {
                    DEBUG << "\tACK: " << recvDatagram.ackNum << ENDL;
                    while (!window.empty() && window.front().seqNum < recvDatagram.ackNum) {
                        window.pop_front();
                    }
                    // Last packet in window should be our EOF which the server won't give
                    // us an ACK for, so we can set allAcked to true.
                    if (window.size() <= 1) {
                        allAcked = true;
                    }
                    timer.stop();
                    break;
                }
 
                // Check to see if the timer has expired.
                if (timer.timeout()) {
                    DEBUG << "Timer expired..." << ENDL;
                    timer.stop();
                    for (datagramS datagram : window) {
                        udt.udt_send(datagram);
                    }
                    timer.setDuration(timeoutThreshold);
                    timer.start();
                }
            }

        }

        // cleanup and close the file and network.
        inputFile.close();
        // Network is cleaned up automatically by destructor.

    } catch (std::exception &e) {
        FATAL<< "Error: " << e.what() << ENDL;
        exit(1);
    }
    return 0;
}
