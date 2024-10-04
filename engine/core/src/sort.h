#ifndef TIKI_SORT_H
#define TIKI_SORT_H

#include "types.h"


void swap_move(move_t *a, move_t *b) {
    move_t temp = *a;
    *a = *b;
    *b = temp;
}

void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int partition(move_t* moves, int *scores, int low, int high) {
    int pivot = scores[high];
    int i = low - 1;

    for (int j = low; j < high; j++) {
        if (scores[j] >= pivot) {
            i++;
            if (i != j) {
                swap(&scores[i], &scores[j]);
                swap_move(&moves[i], &moves[j]);
            }
        }
    }
    if (i + 1 != high) {
        swap(&scores[i + 1], &scores[high]);
        swap_move(&moves[i + 1], &moves[high]);
    }
    return i + 1;
}

void insertion_sort(move_t* moves, int* scores, int low, int high) {
    for (int i = low + 1; i <= high; i++) {
        int key_score = scores[i];
        move_t key_move = moves[i];
        int j = i - 1;

        while (j >= low && scores[j] < key_score) {
            scores[j + 1] = scores[j];
            moves[j + 1] = moves[j];
            j--;
        }
        scores[j + 1] = key_score;
        moves[j + 1] = key_move;
    }
}

void quicksort(move_t* moves, int* scores, int low, int high) {
    while (low < high) {
        if (high - low < 10) {
            insertion_sort(moves, scores, low, high);
            break;
        } else {
            int pi = partition(moves, scores, low, high);
            if (pi - low < high - pi) {
                quicksort(moves, scores, low, pi - 1);
                low = pi + 1;
            } else {
                quicksort(moves, scores, pi + 1, high);
                high = pi - 1;
            }
        }
    }
}

void sort_moves_by_scores(move_t* moves, int *scores, int count) {
    quicksort(moves, scores, 0, count - 1);
}

#endif
