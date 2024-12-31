#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_BUF_SIZE 516
#define TFTP_BLOCK_SIZE 512

// TFTP 操作码
#define RRQ 1  // 读取请求
#define WRQ 2  // 写入请求
#define DATA 3  // 数据包
#define ACK 4  // 确认包
#define ERROR 5  // 错误包

// 数据包结构体
struct tftp_data_packet {
    uint16_t opcode;
    uint16_t block;
    char data[TFTP_BLOCK_SIZE];
};

// TFTP ACK 包
struct tftp_ack_packet {
    uint16_t opcode;
    uint16_t block;
};

// 错误包结构体
struct tftp_error_packet {
    uint16_t opcode;
    uint16_t error_code;
    char error_msg[100];
};

// TFTP 请求包
struct tftp_request_packet {
    uint16_t opcode;
    char filename[100];
    char mode[10];
};

// 错误处理函数
void handle_error(const char *msg) {
    perror(msg);
    exit(1);
}

// 发送 ACK 包
void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, uint16_t block) {
    struct tftp_ack_packet ack_packet;
    ack_packet.opcode = htons(ACK);
    ack_packet.block = htons(block);
    if (sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)client_addr, addr_len) < 0) {
        handle_error("Error sending ACK");
    }
    printf("Sent ACK for block %d\n", block);
}

// 处理读取请求（RRQ）
void handle_rrq(int sockfd, struct sockaddr_in *client_addr, char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        handle_error("Error opening file for reading");
    }

    struct tftp_data_packet data_packet;
    struct tftp_ack_packet ack_packet;
    socklen_t addr_len = sizeof(*client_addr);
    ssize_t bytes_read;
    int block_num = 1;

    while ((bytes_read = fread(data_packet.data, 1, TFTP_BLOCK_SIZE, file)) > 0) {
        data_packet.opcode = htons(DATA);
        data_packet.block = htons(block_num);

        // 发送数据包
        sendto(sockfd, &data_packet, bytes_read + 4, 0, (struct sockaddr *)client_addr, addr_len);
        printf("Sent data block %d, size: %zd bytes\n", block_num, bytes_read);

        // 等待 ACK
        ssize_t ack_len = recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)client_addr, &addr_len);
        if (ack_len < 0) {
            handle_error("Error receiving ACK");
        }

        if (ntohs(ack_packet.block) != block_num) {
            fprintf(stderr, "Unexpected ACK block number, expected %d, got %d\n", block_num, ntohs(ack_packet.block));
            fclose(file);
            return;
        }

        printf("Received ACK for block %d\n", block_num);

        block_num++;

        // 如果读取的字节数小于 512，说明文件传输完毕
        if (bytes_read < TFTP_BLOCK_SIZE) {
            printf("File transfer complete.\n");
            break;
        }
    }

    fclose(file);
}

// 处理写入请求（WRQ）
void handle_wrq(int sockfd, struct sockaddr_in *client_addr, char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        handle_error("Error opening file for writing");
    }

    struct tftp_data_packet data_packet;
    struct tftp_ack_packet ack_packet;
    socklen_t addr_len = sizeof(*client_addr);
    ssize_t bytes_received;
    int block_num = 1;

    // 发送初始 ACK（块号为 0）
    send_ack(sockfd, client_addr, addr_len, 0);

    while (1) {
        // 接收数据包
        bytes_received = recvfrom(sockfd, &data_packet, sizeof(data_packet), 0, (struct sockaddr *)client_addr, &addr_len);
        if (bytes_received < 0) {
            handle_error("Error receiving data");
        }

        if (ntohs(data_packet.opcode) != DATA) {
            fprintf(stderr, "Received incorrect packet type\n");
            fclose(file);
            return;
        }

        if (ntohs(data_packet.block) != block_num) {
            fprintf(stderr, "Unexpected block number, expected %d, got %d\n", block_num, ntohs(data_packet.block));
            continue;  // 忽略不期望的块
        }

        // 写入文件
        fwrite(data_packet.data, 1, bytes_received - 4, file);
        printf("Received data block %d, size: %zd bytes\n", block_num, bytes_received - 4);

        // 发送 ACK
        send_ack(sockfd, client_addr, addr_len, block_num);

        // 如果接收到的字节数小于 512，说明文件传输完毕
        if (bytes_received < TFTP_BLOCK_SIZE + 4) {
            printf("File transfer complete.\n");
            break;
        }

        block_num++;
    }

    fclose(file);
}

// 主程序
int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 创建 UDP 套接字
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        handle_error("Socket creation failed");
    }

    // 初始化服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(69);  // TFTP 默认端口

    // 绑定套接字到端口
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        handle_error("Binding failed");
    }

    printf("TFTP server is running and waiting for requests on port 69...\n");

    // 主循环，接收请求
    while (1) {
        uint8_t buffer[MAX_BUF_SIZE];
        ssize_t received_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (received_len < 0) {
            perror("Error receiving request");
            continue;
        }

        // 解析请求包
        struct tftp_request_packet *request = (struct tftp_request_packet *)buffer;
        uint16_t opcode = ntohs(request->opcode);
        char *filename = request->filename;
        char *mode = request->mode;

        printf("Received request: %s file: %s, mode: %s\n", opcode == RRQ ? "RRQ" : "WRQ", filename, mode);

        // 处理读取请求（RRQ）
        if (opcode == RRQ) {
            handle_rrq(sockfd, &client_addr, filename);
        }
        // 处理写入请求（WRQ）
        else if (opcode == WRQ) {
            handle_wrq(sockfd, &client_addr, filename);
        } else {
            // 处理其他类型的请求
            printf("Invalid request opcode: %d\n", opcode);
        }
    }

    close(sockfd);
    return 0;
}

