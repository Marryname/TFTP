#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>

#define MAX_BUF_SIZE 516
#define TFTP_BLOCK_SIZE 512
#define MAX_INPUT_SIZE 100

// TFTP操作码
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

// 错误码和信息
struct tftp_error_packet {
    uint16_t opcode;
    uint16_t error_code;
    char error_msg[100];
};

// TFTP 请求类型（WRQ）
struct tftp_request_packet {
    uint16_t opcode;
    char filename[100];
    char mode[10];
};

// 通用请求发送函数
void send_request(int sockfd, struct sockaddr_in *server_addr, char *filename, uint16_t opcode) {
    uint8_t request[MAX_BUF_SIZE];
    memset(request, 0, sizeof(request));

    // 设置操作码
    request[0] = 0;
    request[1] = opcode;

    // 设置文件名
    strcpy((char *)(request + 2), filename);

    // 设置模式（一般为 "octet"）
    strcpy((char *)(request + 2 + strlen(filename) + 1), "octet");

    // 发送请求
    sendto(sockfd, request, 2 + strlen(filename) + 1 + strlen("octet") + 1, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Sent request for file: %s\n", filename);
}

// 处理收到的文件数据
void receive_data(int sockfd, struct sockaddr_in *server_addr, char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening file for writing");
        return;
    }

    struct tftp_data_packet data_packet;
    struct tftp_ack_packet ack_packet;
    socklen_t addr_len = sizeof(*server_addr);
    int block = 1;

    while (1) {
        // 接收数据包
        ssize_t bytes_received = recvfrom(sockfd, &data_packet, sizeof(data_packet), 0, (struct sockaddr *)server_addr, &addr_len);
        if (bytes_received < 0) {
            perror("Error receiving data");
            fclose(file);
            return;
        }

        if (ntohs(data_packet.opcode) != DATA || ntohs(data_packet.block) != block) {
            printf("Received incorrect packet or block number\n");
            continue;
        }

        // 写入文件
        fwrite(data_packet.data, 1, bytes_received - 4, file);
        printf("Received DATA block %d\n", block);

        // 发送ACK
        ack_packet.opcode = htons(ACK);
        ack_packet.block = data_packet.block;
        sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)server_addr, addr_len);
        printf("Sent ACK for block %d\n", block);

        if (bytes_received < TFTP_BLOCK_SIZE + 4) {
            printf("File transfer complete.\n");
            break;
        }

        block++;
    }

    fclose(file);
}

// 发送数据块
void send_data(int sockfd, struct sockaddr_in *server_addr, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file for reading");
        exit(EXIT_FAILURE);
    }

    struct tftp_data_packet data_packet;
    struct tftp_ack_packet ack_packet;
    socklen_t addr_len = sizeof(*server_addr);
    ssize_t bytes_read;
    int block_num = 1;  // 块号从 1 开始
    struct timeval timeout;
    timeout.tv_sec = 2;  // 超时时间设为2秒
    timeout.tv_usec = 0;

    // 设置套接字接收超时
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed");
        fclose(file);
        exit(EXIT_FAILURE); 
    }

    // 等待服务器的 ACK (块号0)
    ssize_t ack_len = recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)server_addr, &addr_len);
    if (ack_len < 0) {
        perror("Error receiving ACK for block 0");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    if (ntohs(ack_packet.block) != 0) {
        fprintf(stderr, "Unexpected ACK block number, expected 0, got %d\n", ntohs(ack_packet.block));
        fclose(file);
        exit(EXIT_FAILURE);
    }
    printf("Received ACK for block 0\n");

    // 发送数据块并等待 ACK
    while ((bytes_read = fread(data_packet.data, 1, TFTP_BLOCK_SIZE, file)) > 0) {
        data_packet.opcode = htons(DATA);
        data_packet.block = htons(block_num);

        ssize_t len = sendto(sockfd, &data_packet, bytes_read + 4, 0, (struct sockaddr *)server_addr, addr_len);
        if (len < 0) {
            perror("Error sending data block");
            fclose(file);
            exit(EXIT_FAILURE);
        }

        printf("Sent data block %d, size: %zd bytes\n", block_num, bytes_read);

        // 等待服务器的 ACK
        int retries = 3;  // 设置重试次数
        while (retries-- > 0) {
            ack_len = recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)server_addr, &addr_len);
            if (ack_len < 0) {
                perror("Error receiving ACK, retrying...");
                continue;  // 重试
            }

            // 验证 ACK 包的块号
            if (ntohs(ack_packet.block) == block_num) {
                printf("Received ACK for block %d\n", block_num);
                break;
            } else {
                fprintf(stderr, "Unexpected ACK block number, expected %d, got %d\n", block_num, ntohs(ack_packet.block));
                continue;  // 重试
            }
        }

        // 如果三次重试都失败了，退出
        if (retries <= 0) {
            fprintf(stderr, "Failed to receive correct ACK for block %d\n", block_num);
            fclose(file);
            exit(EXIT_FAILURE);
        }

        block_num++;

        // 如果接收到的字节数小于 512，说明文件传输完毕
        if (bytes_read < TFTP_BLOCK_SIZE) {
            printf("File transfer complete.\n");
            break;
        }
    }

    fclose(file);
}

// 输入函数，处理默认值
void get_input(char *prompt, char *input, const char *default_value) {
    printf("%s", prompt);
    if (fgets(input, MAX_INPUT_SIZE, stdin) != NULL) {
        // 去除输入末尾的换行符
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }
    }

    // 如果用户没有输入，则使用默认值
    if (strlen(input) == 0 && default_value != NULL) {
        strcpy(input, default_value);
    }
}

int main() {
    char server_ip[MAX_INPUT_SIZE] = {0};
    char mode[MAX_INPUT_SIZE] = {0};
    char local_file[MAX_INPUT_SIZE] = {0};
    char remote_file[MAX_INPUT_SIZE] = {0};

    // 输入服务器IP，提供默认值
    get_input("请输入 TFTP 服务器 IP (默认 127.0.0.1): ", server_ip, "127.0.0.1");

    // 输入操作模式，提供默认值
    get_input("请输入模式 (get/put，默认 'get'): ", mode, "get");

    // 输入本地文件名
    get_input("请输入本地文件名: ", local_file, NULL);

    // 输入远程文件名
    get_input("请输入远程文件名: ", remote_file, NULL);

    // 创建套接字
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(69);  // 默认 TFTP 端口
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (strcmp(mode, "get") == 0) {
        send_request(sockfd, &server_addr, remote_file, RRQ);  // 发送 RRQ 请求
        receive_data(sockfd, &server_addr, local_file);  // 接收数据
    } else if (strcmp(mode, "put") == 0) {
        send_request(sockfd, &server_addr, remote_file, WRQ);  // 发送 WRQ 请求
        send_data(sockfd, &server_addr, local_file);  // 发送数据
    } else {
        fprintf(stderr, "Invalid mode. Use 'get' or 'put'.\n");
    }

    close(sockfd);

    return 0;
}

