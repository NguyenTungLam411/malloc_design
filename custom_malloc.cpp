#include <iostream>
#include <unistd.h>

using namespace std;

// ================= MEMORY BLOCK =================
struct MemoryBlock {
    size_t size;
    bool is_allocated;
    MemoryBlock* next_phys; // physical neighbor (for coalescing)

    bool operator<(const MemoryBlock& other) const {
        return size < other.size;
    }

    bool operator>(const MemoryBlock& other) const {
        return size > other.size;
    }
    int heap_index;   // 🔥 NEW
};

// ================= MIN HEAP =================
struct MinHeap {
    MemoryBlock** arr;
    int size;
    int capacity;
};

MinHeap* heap_root = nullptr;

// ================= HEAP UTILS =================
void swap_blocks(MemoryBlock** a, MemoryBlock** b) {
    MemoryBlock* temp = *a;
    *a = *b;
    *b = temp;

    // 🔥 update indices
    (*a)->heap_index = a - heap_root->arr;
    (*b)->heap_index = b - heap_root->arr;
}




void heapify_up(int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (*heap_root->arr[p] > *heap_root->arr[i]) {
            swap_blocks(&heap_root->arr[p], &heap_root->arr[i]);
            i = p;
        } else break;
    }
}

void heapify_down(int i) {
    int smallest = i;
    int l = 2*i + 1;
    int r = 2*i + 2;

    if (l < heap_root->size &&
        *heap_root->arr[l] < *heap_root->arr[smallest])
        smallest = l;

    if (r < heap_root->size &&
        *heap_root->arr[r] < *heap_root->arr[smallest])
        smallest = r;

    if (smallest != i) {
        swap_blocks(&heap_root->arr[i], &heap_root->arr[smallest]);
        heapify_down(smallest);
    }
}

void heap_insert(MemoryBlock* block) {
    if (heap_root->size >= heap_root->capacity) return;

    int i = heap_root->size++;
    heap_root->arr[i] = block;
    block->heap_index = i;   // 🔥 important

    heapify_up(i);
}

// ================= INIT =================
void init_allocator(size_t total_size) {
    void* pool = sbrk(total_size);


    // Heap struct
    heap_root = (MinHeap*)pool;
    heap_root->size = 0;
    heap_root->capacity = 128;

    // Heap array
    heap_root->arr = (MemoryBlock**)((char*)pool + sizeof(MinHeap));

    // First block
    MemoryBlock* first =
        (MemoryBlock*)((char*)heap_root->arr +
                       heap_root->capacity * sizeof(MemoryBlock*));

    first->size = total_size
        - sizeof(MinHeap)
        - heap_root->capacity * sizeof(MemoryBlock*)
        - sizeof(MemoryBlock);

    first->heap_index = -1;
    first->is_allocated = false;
    first->next_phys = nullptr;

    heap_insert(first);
}

void heap_remove(MemoryBlock* block) {
    int i = block->heap_index;
    if (i < 0 || i >= heap_root->size) return;

    // Move last element to position i
    heap_root->arr[i] = heap_root->arr[--heap_root->size];

    if (i < heap_root->size) {
        heap_root->arr[i]->heap_index = i;

        // Restore heap
        heapify_up(i);
        heapify_down(i);
    }

    block->heap_index = -1; // 🔥 mark removed
}

// ================= SPLIT =================
void split_block(MemoryBlock* block, size_t size) {
    if (block->size <= size + sizeof(MemoryBlock)) return;

    MemoryBlock* new_block =
        (MemoryBlock*)((char*)block + sizeof(MemoryBlock) + size);

    new_block->size = block->size - size - sizeof(MemoryBlock);
    new_block->is_allocated = false;
    new_block->next_phys = block->next_phys;
    new_block->heap_index = -1;

    block->size = size;
    block->next_phys = new_block;

    heap_insert(new_block);
}

// ================= MALLOC =================
MemoryBlock* find_best_fit(size_t size) {
    MemoryBlock* best = nullptr;

    for (int i = 0; i < heap_root->size; i++) {
        MemoryBlock* b = heap_root->arr[i];

        if (b->size >= size) {
            if (!best || b->size < best->size) {
                best = b;
            }
        }
    }

    return best;
}


void* malloc1(size_t size) {
    size = (size + 7) & ~7;

    MemoryBlock* block = find_best_fit(size);
    if (!block) return nullptr;

    heap_remove(block);

    if (block->size < size) {
        heap_insert(block); // put it back
    }

    split_block(block, size);
    block->is_allocated = true;

    return (char*)block + sizeof(MemoryBlock);
}


// ================= COALESCE =================
void coalesce(MemoryBlock* block) {
    MemoryBlock* next = block->next_phys;

    if (next && !next->is_allocated) {
        heap_remove(next);   // 🔥 CRITICAL

        block->size += sizeof(MemoryBlock) + next->size;
        block->next_phys = next->next_phys;
    }
}


// ================= FREE =================
void free1(void* ptr) {
    if (!ptr) return;


    MemoryBlock* block =
        (MemoryBlock*)((char*)ptr - sizeof(MemoryBlock));


    block->is_allocated = false;

    coalesce(block);
    heap_insert(block);


}

// ================= TEST =================
struct Node {
    int data;
    Node* next;
};

Node* create_node(int x) {
    Node* n = (Node*)malloc1(sizeof(Node));
    if (n) {
        n->data = x;
        n->next = nullptr;
    }
    return n;
}

void print_list(Node* head) {
    while (head) {
        cout << head->data << " -> ";
        head = head->next;
    }
    cout << "NULL\n";
}

// ================= MAIN =================
int main() {
    init_allocator(1024 * 1024); // 1MB

    Node* head = create_node(10);
    head->next = create_node(20);
    head->next->next = create_node(30);
    head->next->next->next = create_node(40);

    print_list(head);

    free1(head->next->next->next);
    free1(head->next->next);
    free1(head->next);
    free1(head);

    return 0;
}