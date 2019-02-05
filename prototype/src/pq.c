// C code to implement Priority Queue 
// using Linked List   
#include <rte_malloc.h>  

#include "pq.h"
// Node 
  
// Function to Create A New Node 
Node* newNode(void* d, uint32_t p) 
{ 
    Node* temp = (Node*)rte_zmalloc("", sizeof(Node), 0); 
    temp->data = d; 
    temp->priority = p; 
    temp->next = NULL; 
  
    return temp; 
} 
int isEmpty(Node** head) 
{ 
    return (*head) == NULL; 
} 

// Return the value at head 
void* peek(Node** head) 
{ 
    return (*head)->data; 
} 
  
// Removes the element with the 
// highest priority form the list 
void pop(Node** head) 
{ 
    Node* temp = *head; 
    (*head) = (*head)->next;
    rte_free(temp); 
} 
  
// Function to push according to priority 
void push(Node** head, void* d, uint32_t p) 
{ 
    Node* start = (*head); 
  
    // Create new Node 
    Node* temp = newNode(d, p); 
  
    // Special Case: The head of list has lesser 
    // priority than new node. So insert new 
    // node before head node and change head node. 
    if ((*head)->priority > p) { 
  
        // Insert New Node before head 
        temp->next = *head; 
        (*head) = temp; 
    } 
    else { 
  
        // Traverse the list and find a 
        // position to insert new node 
        while (start->next != NULL && 
               start->next->priority < p) { 
            start = start->next; 
        } 
  
        // Either at the ends of the list 
        // or at required position 
        temp->next = start->next; 
        start->next = temp; 
    } 
}