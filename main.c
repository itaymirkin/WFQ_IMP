#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CONNECTIONS 10000
#define MAX_PACKETS 100000
#define MAX_UPDATES 1000
#define MAX_QUEUE_LENGTH 10000

typedef struct {
    int time;
    double weight;
} WeightUpdate;

// Structure for representing a packet
typedef struct {
    int arrival_time;
    int conn_id;     // Index into connections array
    int length;
    double finish_time; // Virtual finish time
    int processed;   // Flag to mark if packet has been output
    double arrived_weight; // Weight at the time of arrival
} Packet;

// Structure for representing a network connection
typedef struct {
    char src_addr[16];
    int src_port;
    char dst_addr[16];
    int dst_port;
    int priority;    // Order of first appearance (0 = highest priority)
    double weight;      // Current weight (default 1)
    WeightUpdate updates[MAX_UPDATES];
    int num_updates;
    double virtual_time; // Virtual time for this connection
    int pending_packets; // Number of packets pending for this connection
    Packet* fifo[MAX_QUEUE_LENGTH];
    int fifo_start;
    int fifo_end;
} Connection;

// Global variables
Connection connections[MAX_CONNECTIONS];
Packet packets[MAX_PACKETS];
int num_connections = 0;
int num_packets = 0;
double global_virtual_time = 0.0;

// Function to find connection and return index 
int find_connection(const char* src_addr, int src_port, const char* dst_addr, int dst_port) {
    for (int i = 0; i < num_connections; i++) {
        if (strcmp(connections[i].src_addr, src_addr) == 0 &&
            connections[i].src_port == src_port &&
            strcmp(connections[i].dst_addr, dst_addr) == 0 &&
            connections[i].dst_port == dst_port) {
            return i;
        }
    }
    return -1; // Not found
}

// Function to add a new connection
int create_connection(const char* src_addr, int src_port, const char* dst_addr, int dst_port) {
    if (num_connections >= MAX_CONNECTIONS) {
        fprintf(stderr, "Error: Maximum number of connections reached.\n");
        return -1;
    }
    int conn_id = num_connections++;
    strcpy(connections[conn_id].src_addr, src_addr);
    connections[conn_id].src_port = src_port;
    strcpy(connections[conn_id].dst_addr, dst_addr);
    connections[conn_id].dst_port = dst_port;
    connections[conn_id].priority = conn_id; // Order of first appearance
    connections[conn_id].weight = 1.0; // Default weight
    connections[conn_id].virtual_time = 0.0; // Initialize virtual time
    connections[conn_id].num_updates = 0;
    connections[conn_id].pending_packets = 0;
    connections[conn_id].fifo_start = 0;
    connections[conn_id].fifo_end = 0;

    return conn_id; // Return the index of the new connection
}

double get_weight_at_time(Connection* conn, int time) {
    double current_weight = 1.0;
    for (int i = 0; i < conn->num_updates; i++) {
        if (conn->updates[i].time <= time) {
            current_weight = conn->updates[i].weight;
        }
        else {
            break;
        }
    }
    return current_weight;
}

double calculate_total_weight(int current_time) {
    double total = 0.0;
    for (int i = 0; i < num_connections; i++) {
        Connection* conn = &connections[i];
        if (conn->fifo_start < conn->fifo_end) {
            Packet* pkt = conn->fifo[conn->fifo_start];
            if (!pkt->processed && pkt->arrival_time <= current_time) {
                // Use the weight that was effective when the packet arrived
                double weight = (pkt->arrived_weight > 0.0) ? pkt->arrived_weight : 1.0;
                total += weight;
            }
        }
    }
    return total > 0.0 ? total : 1.0; // Prevent division by zero
}

void packet_virtual_time(int time, Packet* packet) {
    Connection* conn = &connections[packet->conn_id];

    int byte_length = (packet->length + 7) / 8;
    double weight = (packet->arrived_weight > 0.0) ? packet->arrived_weight : 1.0;
    double total_weight = calculate_total_weight(time);


    // Virtual start time is the maximum of connection's virtual time and current time
    double virtual_start_time = fmax(conn->virtual_time, (double)time);

    // Service time calculation: bytes / (weight/total_weight) = bytes * total_weight / weight
    double service_time = (double)byte_length * total_weight / weight;
    double finish_time = virtual_start_time + service_time;

    // Update connection's virtual time only when we actually process this packet
    // For now, just calculate the finish time
    packet->finish_time = finish_time;
}

// Function to add a packet to the queue
void add_packet(int time, const char* src_addr, int src_port, const char* dst_addr, int dst_port, int length, double weight) {
    if (num_packets >= MAX_PACKETS) {
        fprintf(stderr, "Too many packets\n");
        exit(1);
    }

    // Find if connection already exists, if not create a new connection
    int conn_id = find_connection(src_addr, src_port, dst_addr, dst_port);
    if (conn_id == -1) {
        conn_id = create_connection(src_addr, src_port, dst_addr, dst_port);
        if (conn_id == -1) {
            fprintf(stderr, "Failed to create connection\n");
            return;
        }
    }

    // Update connection weight if specified
    if (weight > 0) {
        Connection* conn = &connections[conn_id];
        if (conn->num_updates < MAX_UPDATES) {
            conn->updates[conn->num_updates].time = time;
            conn->updates[conn->num_updates].weight = weight;
            conn->num_updates++;
        }
        conn->weight = weight; // Update current weight
    }

    Packet temp_packet;
    temp_packet.conn_id = conn_id;
    temp_packet.length = length;
    temp_packet.arrival_time = time;
    temp_packet.processed = 0;

    // Store the effective weight at arrival time
    double effective_weight = (weight > 0) ? weight : get_weight_at_time(&connections[conn_id], time);
    temp_packet.arrived_weight = effective_weight;

    packet_virtual_time(time, &temp_packet);

    packets[num_packets] = temp_packet;
    Packet* new_packet = &packets[num_packets++];

    Connection* conn = &connections[conn_id];
    conn->fifo[conn->fifo_end++] = new_packet;
    conn->pending_packets++;
}

int main() {
    char line[256];
    int time, src_port, dst_port, length;
    char src_addr[16], dst_addr[16];

    // Read all inputs first 
    while (fgets(line, sizeof(line), stdin) != NULL) {
        // Parse the line
        double weight = -1; // Use -1 to indicate no weight specified
        int items = sscanf(line, "%d %15s %d %15s %d %d %lf",
            &time, src_addr, &src_port, dst_addr, &dst_port, &length, &weight);

        if (items < 6) {
            fprintf(stderr, "Invalid input format: %s", line);
            continue;
        }

        // Add packet with or without weight update
        add_packet(time, src_addr, src_port, dst_addr, dst_port, length,
            (items == 7) ? weight : -1);
    }

    int current_time = 0;
    int packets_sent = 0;

    while (packets_sent < num_packets) {
        Packet* best_packet = NULL;
        int best_conn_index = -1;
        double best_finish_time = 0.0;


        

        // Find the best packet to send
        for (int i = 0; i < num_connections; i++) {
            Connection* conn = &connections[i];
            if (conn->fifo_start < conn->fifo_end) {
                Packet* pkt = conn->fifo[conn->fifo_start];
                if (pkt->arrival_time <= current_time && !pkt->processed) {
                    // Recalculate finish time based on current conditions
                    packet_virtual_time(current_time, pkt);
                    
                    // Ensure finish time is not earlier than current time
                    if (pkt->finish_time < current_time) {
                        pkt->finish_time = current_time;
                    }

                    // Choose best packet based on finish time, tie-break by connection priority
                    if (!best_packet ||
                        pkt->finish_time < best_finish_time ||
                        (fabs(pkt->finish_time - best_finish_time) < 1e-9 &&
                            conn->priority < connections[best_conn_index].priority)) {

                        best_packet = pkt;
                        best_conn_index = i;
                        best_finish_time = pkt->finish_time;
                    }
                }
            }
        }
        if (0) {
            if (current_time == 46807) {
                printf("DEBUG at time %d:\n", current_time);
                for (int i = 0; i < num_connections; i++) {
                    Connection* conn = &connections[i];
                    for (int j = conn->fifo_start; j < conn->fifo_end; j++) {
                        Packet* pkt = conn->fifo[j];
                        if (!pkt->processed && pkt->arrival_time <= current_time) {
                            printf("  Packet from src_port=%d: finish_time=%.6f\n",
                                conn->src_port, pkt->finish_time);
                        }
                    }

                }
                printf("Toal Weight is - %d", calculate_total_weight(current_time));

            }
        }
        // If no packets are ready at current_time, advance time
        if (!best_packet) {
            current_time++;
            continue;
        }

        // Send the selected packet
        Connection* conn = &connections[best_conn_index];
        int start_time = (current_time > best_packet->arrival_time)
            ? current_time
            : best_packet->arrival_time;

        printf("%d: %d %s %d %s %d %d",
            start_time,
            best_packet->arrival_time,
            conn->src_addr,
            conn->src_port,
            conn->dst_addr,
            conn->dst_port,
            best_packet->length);

        if (best_packet->arrived_weight > 0.0 && fabs(best_packet->arrived_weight - 1.0) > 1e-6) {
            printf(" %.2f", best_packet->arrived_weight);
        }

        printf("\n");

        // Update connection's virtual time when we actually send the packet
        conn->virtual_time = best_packet->finish_time;

        best_packet->processed = 1;
        conn->fifo_start++;
        conn->pending_packets--;
        packets_sent++;

        current_time = start_time + best_packet->length;
        global_virtual_time = best_packet->finish_time;
    }

    return 0;
}
