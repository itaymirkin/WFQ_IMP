#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CONNECTIONS 10000
#define MAX_PACKETS 100000

// Structure for representing a network connection
typedef struct {
    char src_addr[16];
    int src_port;
    char dst_addr[16];
    int dst_port;
    int priority;    // Order of first appearance (0 = highest priority)
    double weight;      // Current weight (default 1)
    double virtual_time; // Virtual time for this connection
} Connection;

// Structure for representing a packet
typedef struct {
    int arrival_time;
    int conn_id;     // Index into connections array
    int length;
    double finish_time; // Virtual finish time
    int processed;   // Flag to mark if packet has been output
    double arrived_weight; // Weight at the time of arrival (for debugging purposes)
} Packet;

// Global 
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
    if( num_connections >= MAX_CONNECTIONS ) {
		fprintf(stderr, "Error: Maximum number of connections reached.\n");
		return -1;
	}
    int conn_id = num_connections++;
    strcpy(connections[conn_id].src_addr, src_addr);
    connections[conn_id].src_port = src_port;
    strcpy(connections[conn_id].dst_addr, dst_addr);
    connections[conn_id].dst_port = dst_port;
    connections[conn_id].priority = conn_id; // Order of first appearance
    connections[conn_id].weight = 1; // Default weight
    connections[conn_id].virtual_time = 0.0; // Initialize virtual time

    return conn_id; // Return the index of the new connection
}


void packet_virtual_time(int time, Packet* packet) {
    Connection* conn = &connections[packet->conn_id];
    // Calculate the virtual time for the packet based on the connection's weight : start_time = max(connection_virtual_time, packet_arrival_time)
    int byte_length = (packet->length + 7) / 8;

    double start_time = (conn->virtual_time > time) ? conn->virtual_time : time;
    double finish_time = start_time + (double)byte_length / conn->weight;


    conn->virtual_time = finish_time; // Update connection's virtual time

    //Add packet to array
    packets[num_packets].arrival_time = time;
    packets[num_packets].conn_id = packet->conn_id;
    packets[num_packets].length = packet->length;
    packets[num_packets].finish_time = finish_time;
    packets[num_packets].processed = 0; // Mark as unprocessed
    packets[num_packets].arrived_weight = packet->arrived_weight; // Store the weight at the time of arrival
    num_packets++;
}

// Function to add a packet to the queue

void add_packet(int time, const char* src_addr, int src_port, const char* dst_addr, int dst_port, int length, double weight) {
    if (num_packets >= MAX_PACKETS) {
        fprintf(stderr, "Too many packets\n");
        exit(1);
    }
    //find if connection already exists if not create a new connection
    int conn_id = find_connection(src_addr, src_port, dst_addr, dst_port);
    if (conn_id == -1) {
        conn_id = create_connection(src_addr, src_port, dst_addr, dst_port);
		if (conn_id == -1) {
			fprintf(stderr, "Failed to create connection\n");
			return;
		}   
    }
    //printf("Conn wight inside add packet - %d\n", weight);
    if (weight > 0) connections[conn_id].weight = weight; // Update weight if specified

    Packet packet;
	packet.conn_id = conn_id;
	packet.length = length;
	packet.arrival_time = time;
    packet.arrived_weight = weight; // Store the weight at the time of arrival
	// Calculate virtual time for the packet
	packet_virtual_time(time, &packet);
}

// Comparison function for sorting packets by finish time, then by connection priority
int compare_packets(const void* a, const void* b) {
    Packet* p1 = (Packet*)a;
    Packet* p2 = (Packet*)b;

    // First compare by finish time
    if (p1->finish_time < p2->finish_time) return -1;
    if (p1->finish_time > p2->finish_time) return 1;

    // If finish times are equal, compare by connection priority
    int priority1 = connections[p1->conn_id].priority;
    int priority2 = connections[p2->conn_id].priority;

    if (priority1 < priority2) return -1;
    if (priority1 > priority2) return 1;

    return 0;
}


int main() {
    char line[256];
    int time, src_port, dst_port, length;
    
    char src_addr[16], dst_addr[16];
    int max_time = 99999999;//  large value for max time
    // Read all inputs first 
    while (fgets(line, sizeof(line), stdin) != NULL) {
        // Parse the line
        double weight = -1;
        int items = sscanf(line, "%d %15s %d %15s %d %d %lf",
            &time, src_addr, &src_port, dst_addr, &dst_port, &length, &weight);
        //printf("Parsed: %d %s %d %s %d %d %.3f\n", time, src_addr, src_port, dst_addr, dst_port, length, weight );
        if (items < 6) {
            fprintf(stderr, "Invalid input format: %s", line);
            continue;
        }
        // Add packet with or without weight update
        if (items == 7) {
            add_packet(time, src_addr, src_port, dst_addr, dst_port, length, weight);
        }
        else {
            add_packet(time, src_addr, src_port, dst_addr, dst_port, length, -1);
        }

        //use qsort to sort 
        qsort(packets, num_packets, sizeof(Packet), compare_packets);
	}
    int current_real_time = 0;
	// Process packets in order of their finish time
	for (int i = 0; i < num_packets; i++) {
		Packet* packet = &packets[i];
		if (packet->processed) continue; // Skip already processed packets
        Packet* pkt = &packets[i];
        Connection* conn = &connections[pkt->conn_id];

        // Calculate when this packet can actually start transmission
        int start_time = (current_real_time > pkt->arrival_time) ? current_real_time : pkt->arrival_time;

        // If this packet would start after current_time, don't process it yet
        if (start_time > max_time) {
            break;
        }
       
        // Output the packet
            printf("%d: %d %s %d %s %d %d",
                start_time,
                pkt->arrival_time,
                conn->src_addr,
                conn->src_port,
                conn->dst_addr,
                conn->dst_port,
                pkt->length);

        if (pkt->arrived_weight >= 0) {
            printf(" %.2f", pkt->arrived_weight);
        }
        printf("\n");

        // Update current real time
        current_real_time = start_time + (pkt->length+7)/8;
        pkt->processed = 1;

        // Update global virtual time
        global_virtual_time = pkt->finish_time;
	}

	return 0;
}