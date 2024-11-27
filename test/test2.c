#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Complex function with multiple paths
int process_data(int a, int b, int c) {
    int result = 0;
    
    // Path 1: Initial checks
    if (a > b) {
        result += 1;
        if (b > c) {
            result += 10;
        } else {
            result += 20;
        }
    }
    
    // Path 2: Secondary checks with loop
    if (b > 0) {
        int i;
        for (i = 0; i < 3 && i < b; i++) {
            if (a % 2 == 0) {
                result += 5;
            } else {
                result += 3;
            }
        }
    }
    
    // Path 3: Nested conditions
    if (c > 0) {
        if (a > c) {
            result *= 2;
            if (b > c) {
                result += 7;
            }
        } else {
            result += 15;
            if (b < c) {
                result -= 3;
            }
        }
    }
    
    // Path 4: Switch statement
    switch (a % 4) {
        case 0:
            result += 1;
            break;
        case 1:
            if (b % 2 == 0) {
                result += 2;
            }
            break;
        case 2:
            result += 3;
            if (c > 10) {
                result += 4;
            }
            break;
        default:
            result += 5;
            break;
    }
    
    return result;
}

int main() {
    srand(time(NULL));
    int i;
    // Call the function many times with different inputs
    for (i = 0; i < 1000; i++) {
        int a = rand() % 20;
        int b = rand() % 15;
        int c = rand() % 25;
        
        int result = process_data(a, b, c);
        
        // Use the result to prevent optimization
        if (i % 100 == 0) {
            printf("Sample result: %d\n", result);
        }
    }
    
    return 0;
}