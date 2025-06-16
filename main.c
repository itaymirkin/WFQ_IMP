#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "heap.h"

#define MAX_CONNECTIONS 10000
#define MAX_PACKETS 100000
#define MAX_UPDATES 1000
#define MAX_QUEUE_LENGTH 10000

// ==========  KEY STRUCTURES ==========


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
    int has_weight;
    double virtual_arrival_time;
} Packet;

// Structure for representing a network connection
typedef struct {
    char src_addr[16];
    int src_port;
    char dst_addr[16];
    int dst_port;
    int priority;    // Tie-breaking for simultaneous virtual finish times
    double weight;   //Connection weight (default 1.0) ***
    int num_updates;
    //Per-connection virtual time tracking ***
    double virtual_time;     // Virtual time for this connection
    int pending_packets;
    Packet* fifo[MAX_QUEUE_LENGTH];  // FIFO queue per connection
    int fifo_start;
    int fifo_end;
    int active;      // Is connection currently active?
    //int virtual_fifo_start; // Unused - Remove
} Connection;

// Global variables
Connection connections[MAX_CONNECTIONS];
Packet packets[MAX_PACKETS];
int num_connections = 0;
int num_packets = 0;
int current_time = 0;
int next_departure_time = 0;
int packets_sent = 0;


double total_weight = 1;

//Global virtual time for system synchronization ***
double global_virtual_time = 0.0;
double global_virtual_time_last_update = 0.0;

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
    //connections[conn_id].virtual_fifo_start = 0; // Unused - Remove

    return conn_id;
}


void update_global_virtual_time(int real_time) {

    if (real_time > global_virtual_time_last_update) {
        double delta_time = real_time - global_virtual_time_last_update;
        //double total_weight = calculate_total_weight();
        global_virtual_time += delta_time / total_weight; // Use the gloabl total_weight variable
        global_virtual_time_last_update = real_time;
    }
}

void add_packet(int time, const char* src_addr, int src_port, const char* dst_addr, int dst_port, int length, double weight, int has_weight, MinHeap* heap) {
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
    new_packet->has_weight = has_weight;

    // Set packet weight - use connection weight if no specific weight given
    if (has_weight) {
        new_packet->weight = weight;
        conn->weight = weight;  // Update connection weight
    }
    else {
        new_packet->weight = conn->weight;
        //new_packet->weight = 1.00;
    }

    new_packet->virtual_arrival_time = global_virtual_time;
    new_packet->virtual_start_time = fmax(conn->virtual_time, new_packet->virtual_arrival_time);
    new_packet->virtual_finish_time = new_packet->virtual_start_time + ((double)new_packet->length / new_packet->weight);

    HeapNode heap_node = {new_packet->virtual_finish_time, new_packet->arrival_time, new_packet->packet_id, conn_id};

    insert(heap, heap_node);

    if (conn->pending_packets == 0) total_weight += new_packet->weight; // Add weight of the new packet to total weight

    conn->virtual_time = new_packet->virtual_finish_time; // Update connection's latest packet's VFT to be used to calculate the next packet's VFT

    // Add packet to connection's FIFO queue
    conn->fifo[conn->fifo_end] = new_packet;
    conn->fifo_end++;
    conn->pending_packets++;
    num_packets++;
}



int scheduling_loop(int next_time, MinHeap* pkt_heap) {
    // ***  MAIN SCHEDULING LOOP ***
   
        static Packet* best_packet = NULL;
        static int best_conn_index = -1;
        double best_virtual_finish_time = INFINITY;
        static  Connection* conn = NULL;
        //double total_weight = calculate_total_weight();

        static int start_time;

        // Update the queue when the packet actually finish sending to maintain correct virtual time  
        if (best_packet != NULL)
        {
            // Send the selected packet
            
            printf("%d: %d %s %d %s %d %d",
                start_time,
                best_packet->arrival_time,
                conn->src_addr,
                conn->src_port,
                conn->dst_addr,
                conn->dst_port,
                best_packet->length);

            if (best_packet->has_weight) {
                printf(" %.2f", best_packet->weight);
            }

            printf("\n");


            // Mark packet as processed and update queues
            best_packet->processed = 1;

            total_weight -= best_packet->weight; // Remove weight of the packet being sent
            conn->fifo_start++;
            conn->pending_packets--;

            if (conn->pending_packets > 0)
            {
                total_weight += conn->fifo[conn->fifo_start]->weight; // Add weight of the next packet in the FIFO queue
            }

            packets_sent++;
        }

        best_packet = NULL; // Reset best packet for next iteration to track if a packet was found
        

        // Debug output for specific time
        if (0) {
            if (current_time == 316819) {
                printf("DEBUG at time %d (Global VT: %.6f):\n", current_time, global_virtual_time);

                for (int i = 0; i < num_connections; i++) {
                    Connection* conn = &connections[i];
                    //if (conn->fifo_start >= conn->fifo_end) continue;
                    if (conn->pending_packets == 0) continue;

                    Packet* pkt = conn->fifo[conn->fifo_start];
                    if (!pkt->processed && pkt->arrival_time <= current_time) {
                        //calculate_packet_virtual_times(pkt, total_weight);
                        printf("  Conn %d (port %d): VFT=%.6f, weight=%.2f, length=%d, arrival=%d, VAT=%.6f, VST=%.6f, Previous Packet VFT=%.6f, Total Weight=%.2f\n",
                            i, conn->src_port, pkt->virtual_finish_time,
                            pkt->weight, pkt->length, pkt->arrival_time, pkt->virtual_arrival_time,pkt->virtual_start_time, conn->fifo[conn->fifo_start-1]->virtual_finish_time, total_weight);
                    }
                }

                if (best_packet) {
                    printf("  SELECTED: Conn %d (port %d), VFT=%.6f\n",
                        best_conn_index, connections[best_conn_index].src_port,
                        best_packet->virtual_finish_time);
                }
                
            }
        }

        // If no packets are ready, advance time
        if (pkt_heap->size == 0) {
            
            
            next_departure_time = next_time; // If there are not packets ready, just advance time to the next input
            return 1;
        }

        else
        {
            int conn_id = extract_min(pkt_heap).flow_id;
            conn = &connections[conn_id];
            best_packet = conn->fifo[conn->fifo_start]; // Extract the packet with the earliest virtual finish time
        }
        // Set the start time according to time of the last departure or the packet arrival time
        start_time = fmax(next_departure_time, best_packet->arrival_time);

        // Advance current time by packet transmission time
        next_departure_time = start_time + best_packet->length;

        
        // If successfully scheduled a packet return 0
        return 0; 


    
}

// ========== MAIN SCHEDULING LOOP ==========

int main() {
    char line[256];
    int time, src_port, dst_port, length;
    char src_addr[16], dst_addr[16];

    // This is not being used - Consider using these to determine schedueling
    // Set up heap for virtual finish time
    MinHeap* pkt_heap = create_heap(0);

    // Initialize all virtual times to 0
    global_virtual_time = 0.0;
    for (int i = 0; i < num_connections; i++) {
        connections[i].virtual_time = 0.0;
    }

    // Read all inputs first 
    while (fgets(line, sizeof(line), stdin) != NULL) {
        double weight = -1.0;
        int items = sscanf(line, "%d %15s %d %15s %d %d %lf",
            &time, src_addr, &src_port, dst_addr, &dst_port, &length, &weight);

        if (items < 6) {
            fprintf(stderr, "Invalid input format: %s", line);
            continue;
        }

        while (time >= next_departure_time)
        {
            update_global_virtual_time(next_departure_time);
            current_time = next_departure_time;
            if (scheduling_loop(time, pkt_heap) == 1) break;
        }


       
        update_global_virtual_time(time); // Update global virtual time based on real time input
        add_packet(time, src_addr, src_port, dst_addr, dst_port, length, weight, (items == 7) ? 1: 0, pkt_heap);
        
        current_time = time;
    }

    
    while (packets_sent < num_packets) 
    {
        update_global_virtual_time(next_departure_time);
        current_time = next_departure_time;
        scheduling_loop(current_time, pkt_heap);
    }

    free_heap(pkt_heap);
    return 0;
}
