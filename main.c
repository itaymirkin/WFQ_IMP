#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CONNECTIONS 10000
#define MAX_PACKETS 100000
#define MAX_UPDATES 1000
#define MAX_QUEUE_LENGTH 10000

// ==========  KEY STRUCTURES ==========

typedef struct {
    int time;
    float weight;
} WeightUpdate;

// Structure for representing a packet
typedef struct {
    int arrival_time;
    int conn_id;
    int length;
    //  Virtual timing for fair scheduling ***
    double virtual_start_time;   // When packet starts service in virtual time
    double virtual_finish_time;  // When packet finishes service in virtual time
    int processed;
    double weight;  // Weight affects virtual service time
    int packet_id;  // For debugging
} Packet;

// Structure for representing a network connection
typedef struct {
    char src_addr[16];
    int src_port;
    char dst_addr[16];
    int dst_port;
    int priority;    // Tie-breaking for simultaneous virtual finish times
    double weight;   //Connection weight (default 1.0) ***
    WeightUpdate updates[MAX_UPDATES];
    int num_updates;
    //Per-connection virtual time tracking ***
    double virtual_time;     // Virtual time for this connection
    int pending_packets;
    Packet* fifo[MAX_QUEUE_LENGTH];  // FIFO queue per connection
    int fifo_start;
    int fifo_end;
    int active;      // Is connection currently active?
} Connection;

// Global variables
Connection connections[MAX_CONNECTIONS];
Packet packets[MAX_PACKETS];
int num_connections = 0;
int num_packets = 0;
//Global virtual time for system synchronization ***
double global_virtual_time = 0.0;

// ========== CONNECTION MANAGEMENT ==========

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
    if (num_connections >= MAX_CONNECTIONS) {
        fprintf(stderr, "Error: Maximum number of connections reached.\n");
        return -1;
    }
    int conn_id = num_connections++;
    strcpy(connections[conn_id].src_addr, src_addr);
    connections[conn_id].src_port = src_port;
    strcpy(connections[conn_id].dst_addr, dst_addr);
    connections[conn_id].dst_port = dst_port;
    connections[conn_id].priority = conn_id;
    connections[conn_id].weight = 1.0;  // *** : Default weight ***
    connections[conn_id].virtual_time = 0.0;  // *** : Initialize virtual time ***
    connections[conn_id].num_updates = 0;
    connections[conn_id].pending_packets = 0;
    connections[conn_id].fifo_start = 0;
    connections[conn_id].fifo_end = 0;
    connections[conn_id].active = 0;
    return conn_id;
}



void add_packet(int time, const char* src_addr, int src_port, const char* dst_addr, int dst_port, int length, double weight) {
    if (num_packets >= MAX_PACKETS) {
        fprintf(stderr, "Too many packets\n");
        exit(1);
    }

    // Find or create connection
    int conn_id = find_connection(src_addr, src_port, dst_addr, dst_port);
    if (conn_id == -1) {
        conn_id = create_connection(src_addr, src_port, dst_addr, dst_port);
        if (conn_id == -1) {
            fprintf(stderr, "Failed to create connection\n");
            return;
        }
    }
    Connection* conn = &connections[conn_id];

    // Create and initialize new packet
    Packet* new_packet = &packets[num_packets];
    new_packet->conn_id = conn_id;
    new_packet->length = length;
    new_packet->arrival_time = time;
    new_packet->processed = 0;
    new_packet->packet_id = num_packets;

    // Set packet weight - use connection weight if no specific weight given
    if (weight > 0) {
        new_packet->weight = weight;
        conn->weight = weight;  // Update connection weight
    }
    else {
        new_packet->weight = conn->weight;
    }

    // Add packet to connection's FIFO queue
    conn->fifo[conn->fifo_end++] = new_packet;
    conn->pending_packets++;
    num_packets++;
}

// ***  FUNCTION: Calculate total weight of all connections ***
double calculate_total_weight() {
    double total = 0.0;
    for (int i = 0; i < num_connections; i++) {
        total += connections[i].weight;
    }
    return total > 0.0 ? total : 1.0; // Prevent division by zero
}

// ***  FUNCTION: Calculate packet virtual times dynamically ***
void calculate_packet_virtual_times(Packet* packet) {
    Connection* conn = &connections[packet->conn_id];

    packet->virtual_start_time = fmax(conn->virtual_time, global_virtual_time);

    double total_weight = calculate_total_weight();
    // Proper  formula: service_time = (packet_length / connection_weight) * total_weight
    double service_time = ((double)packet->length / packet->weight) * total_weight;

    packet->virtual_finish_time = packet->virtual_start_time + service_time;
}
// ========== MAIN SCHEDULING LOOP ==========

int main() {
    char line[256];
    int time, src_port, dst_port, length;
    char src_addr[16], dst_addr[16];

    // Read all inputs first 
    while (fgets(line, sizeof(line), stdin) != NULL) {
        double weight = -1.0;
        int items = sscanf(line, "%d %15s %d %15s %d %d %lf",
            &time, src_addr, &src_port, dst_addr, &dst_port, &length, &weight);

        if (items < 6) {
            fprintf(stderr, "Invalid input format: %s", line);
            continue;
        }

        add_packet(time, src_addr, src_port, dst_addr, dst_port, length,
            (items == 7) ? weight : -1.0);
    }

    printf("Loaded %d packets across %d connections\n", num_packets, num_connections);

    int current_time = 0;
    int packets_sent = 0;

    // Initialize all virtual times to 0
    global_virtual_time = 0.0;
    for (int i = 0; i < num_connections; i++) {
        connections[i].virtual_time = 0.0;
    }

    // ***  MAIN SCHEDULING LOOP ***
    while (packets_sent < num_packets) {
        Packet* best_packet = NULL;
        int best_conn_index = -1;
        double best_virtual_finish_time = INFINITY;

        // Calculate virtual times for all eligible packets and find the best one
        for (int i = 0; i < num_connections; i++) {
            Connection* conn = &connections[i];

            if (conn->fifo_start >= conn->fifo_end) continue;

            Packet* pkt = conn->fifo[conn->fifo_start];

            if (!pkt->processed && pkt->arrival_time <= current_time) {
                // Calculate virtual times for this packet
                calculate_packet_virtual_times(pkt);

                // ***  SELECTION RULE: Earliest virtual finish time wins ***
                int is_better = 0;

                if (pkt->virtual_finish_time < best_virtual_finish_time - 1e-9) {
                    is_better = 1;
                }
                else if (fabs(pkt->virtual_finish_time - best_virtual_finish_time) < 1e-9) {
                    // Tie in VFT - use arrival time as tie breaker
                    if (pkt->arrival_time < best_packet->arrival_time) {
                        is_better = 1;
                    }
                    else if (pkt->arrival_time == best_packet->arrival_time) {
                        // Still tied - use packet ID (creation order)
                        if (pkt->packet_id < best_packet->packet_id) {
                            is_better = 1;
                        }
                    }
                }

                if (is_better) {
                    best_packet = pkt;
                    best_conn_index = i;
                    best_virtual_finish_time = pkt->virtual_finish_time;
                }
            }
        }

        // Debug output for specific time
        if (current_time == 312490) {
            printf("DEBUG at time %d (Global VT: %.6f):\n", current_time, global_virtual_time);

            for (int i = 0; i < num_connections; i++) {
                Connection* conn = &connections[i];
                if (conn->fifo_start >= conn->fifo_end) continue;

                Packet* pkt = conn->fifo[conn->fifo_start];
                if (!pkt->processed && pkt->arrival_time <= current_time) {
                    calculate_packet_virtual_times(pkt);
                    printf("  Conn %d (port %d): VFT=%.6f, weight=%.2f, length=%d, arrival=%d, VST=%.6f\n",
                        i, conn->src_port, pkt->virtual_finish_time,
                        pkt->weight, pkt->length, pkt->arrival_time, pkt->virtual_start_time);
                }
            }

            if (best_packet) {
                printf("  SELECTED: Conn %d (port %d), VFT=%.6f\n",
                    best_conn_index, connections[best_conn_index].src_port,
                    best_packet->virtual_finish_time);
            }
            break;
        }

        // If no packets are ready, advance time
        if (!best_packet) {
            current_time++;
            continue;
        }

        // Send the selected packet
        Connection* conn = &connections[best_conn_index];
        int start_time = fmax(current_time, best_packet->arrival_time);

        printf("%d: %d %s %d %s %d %d",
            start_time,
            best_packet->arrival_time,
            conn->src_addr,
            conn->src_port,
            conn->dst_addr,
            conn->dst_port,
            best_packet->length);

        if (fabs(best_packet->weight - 1.0) > 1e-6) {
            printf(" %.2f", best_packet->weight);
        }

        printf("\n");

        // *** : Update connection virtual time after scheduling ***
        conn->virtual_time = best_packet->virtual_finish_time;

        // Mark packet as processed and update queues
        best_packet->processed = 1;
        conn->fifo_start++;
        conn->pending_packets--;
        packets_sent++;

        // Advance current time by packet transmission time
        current_time = start_time + best_packet->length;

        // *** : Update global virtual time ***
        global_virtual_time = fmax(global_virtual_time, best_packet->virtual_finish_time);
    }

    return 0;
}
