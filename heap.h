#ifndef MIN_HEAP_H
#define MIN_HEAP_H

typedef struct {
    double finish_time;
    int arrival_time;
    int packet_id;
    int flow_id;
} HeapNode;

typedef struct {
    HeapNode* data;
    int size;
    int capacity;
} MinHeap;

MinHeap* create_heap(int capacity);
void insert(MinHeap* heap, HeapNode node);
HeapNode extract_min(MinHeap* heap);
void free_heap(MinHeap* heap);
int heap_size(MinHeap* heap);
static int compare(const HeapNode* a, const HeapNode* b);

#endif
