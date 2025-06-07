#include <stdio.h>
#include <stdlib.h>
#include "heap.h"



static void swap(HeapNode* a, HeapNode* b) {
    HeapNode temp = *a;
    *a = *b;
    *b = temp;
}

static void heapify_up(MinHeap* heap, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap->data[index].finish_time < heap->data[parent].finish_time) {
            swap(&heap->data[index], &heap->data[parent]);
            index = parent;
        }
        else break;
    }
}

static void heapify_down(MinHeap* heap, int index) {
    int smallest = index;
    int left = 2 * index + 1;
    int right = 2 * index + 2;

    if (left < heap->size && heap->data[left].finish_time < heap->data[smallest].finish_time)
        smallest = left;
    if (right < heap->size && heap->data[right].finish_time < heap->data[smallest].finish_time)
        smallest = right;

    if (smallest != index) {
        swap(&heap->data[index], &heap->data[smallest]);
        heapify_down(heap, smallest);
    }
}

MinHeap* create_heap(int capacity) {
    if (capacity <= 0) capacity = 16; // minimal default
    MinHeap* heap = (MinHeap*)malloc(sizeof(MinHeap));
    heap->data = (HeapNode*)malloc(sizeof(HeapNode) * capacity);
    heap->size = 0;
    heap->capacity = capacity;
    return heap;
}

void insert(MinHeap* heap, HeapNode node) {
    if (heap->size >= heap->capacity) {
        // Double the capacity
        heap->capacity *= 2;
        heap->data = (HeapNode*)realloc(heap->data, heap->capacity * sizeof(HeapNode));
        if (!heap->data) {
            printf("Heap memory allocation failed!\n");
            exit(1);
        }
    }
    heap->data[heap->size] = node;
    heapify_up(heap, heap->size);
    heap->size++;
}

HeapNode extract_min(MinHeap* heap) {
    if (heap->size == 0) {
        printf("Heap empty!\n");
        exit(1);
    }
    HeapNode min = heap->data[0];
    heap->data[0] = heap->data[--heap->size];
    heapify_down(heap, 0);
    return min;
}

void free_heap(MinHeap* heap) {
    free(heap->data);
    free(heap);
}

int heap_size(MinHeap* heap) {
    return heap->size;
}
