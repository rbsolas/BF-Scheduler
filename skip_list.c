#include "types.h"
#include "defs.h"
// #include "skip_list.h"

// Function to initialize a new sorted skip list
struct SkipList* initSkipList() {
    struct SkipList* skipList = (struct SkipList*)kalloc(); // malloc(sizeof(struct SkipList))
    skipList->head = (struct SkipNode*)kalloc(); // malloc(sizeof(struct SkipNode))
    skipList->level = 0;

    // Initialize head nodes
    for (int i = 0; i < 4; i++) {
        skipList->head->forward[i] = 0;
        skipList->head->backward[i] = 0;
    }

    return skipList;
}

unsigned int seed = SEED;
unsigned int random(int max) {
    seed ^= seed << 17;
    seed ^= seed >> 7;
    seed ^= seed << 5;
    return seed % max;
}

// Function to up a level by chance for a new element
int slUpLevel(float p) {
    int level = 0;
    while ((random(100) / 100.0) < p && level < MAX_LEVEL - 1) {
        level++;
    }
    return level;
}

// Function to insert a value into the sorted skip list
void slInsert(struct SkipList* skipList, int value, float p) {
    struct SkipNode* update[4];
    struct SkipNode* current = skipList->head;

    // printf(1, "Inserting value %d:\n", value);

    for (int i = skipList->level; i >= 0; i--) {
        while (current->forward[i] != 0 && current->forward[i]->value < value) {
            // printf(1, "  Moving right at level %d (current value: %d)\n", i, current->forward[i]->value);
            current = current->forward[i];
        }
        update[i] = current;
        // printf(1, "  Reached the rightmost node at level %d (current value: %d)\n", i, current->value);
    }

    int newLevel = slUpLevel(p);

    if (newLevel > skipList->level) {
        for (int i = skipList->level + 1; i <= newLevel; i++) {
            update[i] = skipList->head;
        }
        skipList->level = newLevel;
        // printf(1, "Increased skip list level to %d\n", skipList->level);
    }

    struct SkipNode* newNode = (struct SkipNode*)kalloc(); // malloc(sizeof(struct SkipNode))
    newNode->value = value;

    for (int i = 0; i <= newLevel; i++) {
        newNode->forward[i] = update[i]->forward[i];
        if (update[i]->forward[i] != 0) {
            update[i]->forward[i]->backward[i] = newNode;
        }
        update[i]->forward[i] = newNode;

        newNode->backward[i] = update[i];
        
        // printf(1, "  Inserted at level %d (forward pointer value: %d, backward pointer value: %d)\n", i, newNode->forward[i]->value, newNode->backward[i]->value);
    }

    // printf(1, "Value %d inserted successfully\n", value);
}

// Function to search for a value in the sorted skip list
struct SkipNode* slSearch(struct SkipList* skipList, int value) {
    struct SkipNode* current = skipList->head;

    // printf(1, "Searching for value %d:\n", value);

    for (int i = skipList->level; i >= 0; i--) {
        // printf(1, "  Checking level %d (current value: %d)\n", i, current->value);
        while (current->forward[i] != 0 && current->forward[i]->value < value) {
            // printf(1, "    Moving right at level %d (current value: %d)\n", i, current->forward[i]->value);
            current = current->forward[i];
        }

        if (current->forward[i] != 0 && current->forward[i]->value == value) {
            // printf(1, "Value %d found at level %d\n", value, i);
            return current->forward[i];
        }

        if (i > 0) { // Just to verify if the downward functionality of the skip list works.
            // printf(1, "  Downward next value at level %d: %d\n", i-1, current->forward[i-1]->value);
        }
    }

    // printf(1, "Value %d not found\n", value);
    return 0;
}



// Function to print the entire skip list
void printSkipList(struct SkipList* skipList) {
    // printf(1, "Skip List:\n");
    for (int i = skipList->level; i >= 0; i--) {
        struct SkipNode* current = skipList->head->forward[i];
        // printf(1, "Level %d: ", i);
        while (current != 0) {
            // printf(1, "%d -> ", current->value);
            current = current->forward[i];
        }
        // printf(1, "0\n");
    }
}