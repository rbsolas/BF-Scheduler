#define BFS_DEFAULT_QUANTUM 50       //  Length of quantum (in ticks) assigned by default to each process
#define BFS_NICE_FIRST_LEVEL -20      // Most negative possible nice value; assumed to be set to at most 0
#define BFS_NICE_LAST_LEVEL 19       // Most positive possible nice value; assumed never to be set less than FIRST LEVEL
#define PRIO_RATIO(n) n + 21     // Converts Niceness into Prioratio


#define CHANCE 0.25
#define MAX_SKIPLIST_LEVEL 4
#define SEED 62301983