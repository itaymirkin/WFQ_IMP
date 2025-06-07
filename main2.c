#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CONNECTIONS 10000
#define MAX_PACKETS 100000
#define MAX_QUEUE 10000
#define INF 1000000000

typedef struct {
    int arrival_time;
    int conn_id;
    int length;
    double weight;
    double virtual_start_time;
    double virtual_finish_time;
} Packet;

typedef struct {
    char src_addr[16];
    int src_port;
    char dst_addr[16];
    int dst_port;
    double weight;
    double virtual_time;
    Packet* fifo[MAX_QUEUE];
    int fifo_start;
    int fifo_end;
    int active;
} Connection;

typedef struct {
    Packet* packet;
} SchedulerItem;

Connection connections[MAX_CONNECTIONS];
Packet packets[MAX_PACKETS];
SchedulerItem scheduler[MAX_CONNECTIONS];

int num_connections = 0;
int num_packets = 0;
int scheduler_size = 0;
double virtual_time = 0.0;
int real_time = 0;
int arrival_index = 0;

// --- Scheduler heap for virtual finish time ---

void scheduler_swap(int i, int j) {
    SchedulerItem tmp = scheduler[i];
    scheduler[i] = scheduler[j];
    scheduler[j] = tmp;
}

void scheduler_push(Packet* pkt) {
    int i = scheduler_size++;
    scheduler[i].packet = pkt;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (scheduler[parent].packet->virtual_finish_time <= scheduler[i].packet->virtual_finish_time)
            break;
        scheduler_swap(i, parent);
        i = parent;
    }
}

Packet* scheduler_peek() {
    return scheduler[0].packet;
}

Packet* scheduler_pop() {
    Packet* top = scheduler[0].packet;
    scheduler[0] = scheduler[--scheduler_size];
    int i = 0;
    while (1) {
        int smallest = i;
        int l = 2 * i + 1, r = 2 * i + 2;
        if (l < scheduler_size && scheduler[l].packet->virtual_finish_time < scheduler[smallest].packet->virtual_finish_time)
            smallest = l;
        if (r < scheduler_size && scheduler[r].packet->virtual_finish_time < scheduler[smallest].packet->virtual_finish_time)
            smallest = r;
        if (smallest == i)
            break;
        scheduler_swap(i, smallest);
        i = smallest;
    }
    return top;
}

int find_connection(const char* src_addr, int src_port, const char* dst_addr, int dst_port) {
    for (int i = 0; i < num_connections; i++) {
        if (strcmp(connections[i].src_addr, src_addr) == 0 &&
            connections[i].src_port == src_port &&
            strcmp(connections[i].dst_addr, dst_addr) == 0 &&
            connections[i].dst_port == dst_port) {
            return i;
        }
    }
    return -1;
}

int create_connection(const char* src_addr, int src_port, const char* dst_addr, int dst_port) {
    int conn_id = num_connections++;
    strcpy(connections[conn_id].src_addr, src_addr);
    connections[conn_id].src_port = src_port;
    strcpy(connections[conn_id].dst_addr, dst_addr);
    connections[conn_id].dst_port = dst_port;
    connections[conn_id].weight = 1.0;
    connections[conn_id].virtual_time = 0.0;
    connections[conn_id].fifo_start = 0;
    connections[conn_id].fifo_end = 0;
    connections[conn_id].active = 0;
    return conn_id;
}

void arrival(Packet* pkt) {
    Connection* conn = &connections[pkt->conn_id];

    if (pkt->arrival_time > real_time)
        virtual_time += pkt->arrival_time - real_time;

    pkt->virtual_start_time = fmax(conn->virtual_time, virtual_time);
    pkt->virtual_finish_time = pkt->virtual_start_time + (double)pkt->length / pkt->weight;
    conn->virtual_time = pkt->virtual_finish_time;

    conn->fifo[conn->fifo_end++] = pkt;

    if (!conn->active) {
        conn->active = 1;
        scheduler_push(pkt);
    }
}

void scheduler_loop() {
    while (scheduler_size > 0 || arrival_index < num_packets) {
        int next_arrival_time = (arrival_index < num_packets) ? packets[arrival_index].arrival_time : INF;

        if (scheduler_size == 0 || next_arrival_time <= real_time) {
            // Process arrival
            real_time = next_arrival_time;
            arrival(&packets[arrival_index++]);
            continue;
        }

        Packet* pkt = scheduler_peek();
        if (pkt->arrival_time > real_time) {
            // Can't send before arrival
            real_time = pkt->arrival_time;
            continue;
        }

        pkt = scheduler_pop();
        Connection* conn = &connections[pkt->conn_id];

        printf("%d: %d %s %d %s %d %d %.2f\n",
            real_time, pkt->arrival_time, conn->src_addr, conn->src_port,
            conn->dst_addr, conn->dst_port, pkt->length, pkt->weight);

        real_time += pkt->length;
        virtual_time += pkt->length;

        conn->fifo_start++;
        if (conn->fifo_start == conn->fifo_end) {
            conn->active = 0;
        }
        else {
            Packet* next_pkt = conn->fifo[conn->fifo_start];
            scheduler_push(next_pkt);
        }
    }
}

int main() {
    char line[256];
    int time, src_port, dst_port, length;
    char src_addr[16], dst_addr[16];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        double weight = 1.0;
        int items = sscanf(line, "%d %15s %d %15s %d %d %lf",
            &time, src_addr, &src_port, dst_addr, &dst_port, &length, &weight);

        int conn_id = find_connection(src_addr, src_port, dst_addr, dst_port);
        if (conn_id == -1)
            conn_id = create_connection(src_addr, src_port, dst_addr, dst_port);
        Connection* conn = &connections[conn_id];

        Packet* pkt = &packets[num_packets++];
        pkt->arrival_time = time;
        pkt->conn_id = conn_id;
        pkt->length = length;
        pkt->weight = weight;
        conn->weight = weight;
    }

    scheduler_loop();
    return 0;
}
