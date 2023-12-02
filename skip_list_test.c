#include "types.h"
#include "user.h"

#define CHANCE 0.75
#define MAX_LEVEL 4
#define SEED 123456789

// node structure for the skip list
struct SkipNode {
    int value;
    struct SkipNode* forward[MAX_LEVEL];
    struct SkipNode* backward[MAX_LEVEL]; // note: doubly linked
};

// skip list structure
struct SkipList {
    struct SkipNode* head;
    int level;  // Current level of the skip list
};

// Function to initialize a new sorted skip list
struct SkipList* initSkipList() {
    struct SkipList* skipList = malloc(sizeof(struct SkipList));
    skipList->head = malloc(sizeof(struct SkipNode));
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
int upLevel(float p) {
    int level = 0;
    while ((random(100) / 100.0) < p && level < MAX_LEVEL - 1) {
        level++;
    }
    return level;
}

// Function to insert a value into the sorted skip list
void insert(struct SkipList* skipList, int value, float p) {
    struct SkipNode* update[4];
    struct SkipNode* current = skipList->head;

    printf(1, "Inserting value %d:\n", value);

    for (int i = skipList->level; i >= 0; i--) {
        while (current->forward[i] != 0 && current->forward[i]->value < value) {
            printf(1, "  Moving right at level %d (current value: %d)\n", i, current->forward[i]->value);
            current = current->forward[i];
        }
        update[i] = current;
        printf(1, "  Reached the rightmost node at level %d (current value: %d)\n", i, current->value);
    }

    int newLevel = upLevel(p);

    if (newLevel > skipList->level) {
        for (int i = skipList->level + 1; i <= newLevel; i++) {
            update[i] = skipList->head;
        }
        skipList->level = newLevel;
        printf(1, "Increased skip list level to %d\n", skipList->level);
    }

    struct SkipNode* newNode = malloc(sizeof(struct SkipNode));
    newNode->value = value;

    for (int i = 0; i <= newLevel; i++) {
        newNode->forward[i] = update[i]->forward[i];
        if (update[i]->forward[i] != 0) {
            update[i]->forward[i]->backward[i] = newNode;
        }
        update[i]->forward[i] = newNode;

        newNode->backward[i] = update[i];
        
        printf(1, "  Inserted at level %d (forward pointer value: %d, backward pointer value: %d)\n", i, newNode->forward[i]->value, newNode->backward[i]->value);
    }

    printf(1, "Value %d inserted successfully\n", value);
}

// Function to search for a value in the sorted skip list
struct SkipNode* search(struct SkipList* skipList, int value) {
    struct SkipNode* current = skipList->head;

    printf(1, "Searching for value %d:\n", value);

    for (int i = skipList->level; i >= 0; i--) {
        printf(1, "  Checking level %d (current value: %d)\n", i, current->value);
        while (current->forward[i] != 0 && current->forward[i]->value < value) {
            printf(1, "    Moving right at level %d (current value: %d)\n", i, current->forward[i]->value);
            current = current->forward[i];
        }

        if (current->forward[i] != 0 && current->forward[i]->value == value) {
            printf(1, "Value %d found at level %d\n", value, i);
            return current->forward[i];
        }

        if (i > 0) { // Just to verify if the downward functionality of the skip list works.
            printf(1, "  Downward next value at level %d: %d\n", i-1, current->forward[i-1]->value);
        }
    }

    printf(1, "Value %d not found\n", value);
    return 0;
}



// Function to print the entire skip list
void printSkipList(struct SkipList* skipList) {
    printf(1, "Skip List:\n");
    for (int i = skipList->level; i >= 0; i--) {
        struct SkipNode* current = skipList->head->forward[i];
        printf(1, "Level %d: ", i);
        while (current != 0) {
            printf(1, "%d -> ", current->value);
            current = current->forward[i];
        }
        printf(1, "0\n");
    }
}


// Test program
int main() {
    struct SkipList* skipList = initSkipList();

    insert(skipList, 30, CHANCE);
    printSkipList(skipList);

    insert(skipList, 60, CHANCE);
    printSkipList(skipList);

    insert(skipList, 90, CHANCE);
    printSkipList(skipList);

    insert(skipList, 40, CHANCE);
    printSkipList(skipList);

    insert(skipList, 20, CHANCE);
    printSkipList(skipList);

    insert(skipList, 80, CHANCE);
    printSkipList(skipList);

    insert(skipList, 70, CHANCE);
    printSkipList(skipList);

    insert(skipList, 50, CHANCE);
    printSkipList(skipList);

    insert(skipList, 10, CHANCE);
    insert(skipList, 45, CHANCE);
    insert(skipList, 85, CHANCE);
    insert(skipList, 95, CHANCE);

    printSkipList(skipList);

    // Search for values
    struct SkipNode* result1 = search(skipList, 60);
    struct SkipNode* result2 = search(skipList, 20);
    struct SkipNode* result3 = search(skipList, 85);
    struct SkipNode* result4 = search(skipList, 100);

    struct SkipNode* results[] = {result1, result2, result3, result4};
    int values[] = {60, 20, 85, 100};

    for (int i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        if (results[i] != 0) {
            printf(1, "Value %d found\n", results[i]->value);
        } else {
            printf(1, "Value %d not found\n", values[i]);
        }
    }

    exit();
}