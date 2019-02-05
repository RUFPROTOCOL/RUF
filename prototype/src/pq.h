#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stdio.h> 
#include <stdlib.h> 

typedef struct node { 
    void* data; 
  
    // Lower values indicate higher priority 
    uint32_t priority; 
  
    struct node* next; 
  
} Node; 

int isEmpty(Node** head);
Node* newNode(void* d, uint32_t p);
void* peek(Node** head);
void pop(Node** head);
void push(Node** head, void* d, uint32_t p); 
#endif