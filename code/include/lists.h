typedef struct Node
{
    char addr[IP_LEN];
    char status;
    struct Node *next;
} MAIL_LIST;

void init(MAIL_LIST **head)
{
    *head = NULL;
}

MAIL_LIST *make_node(char *addr, char status)
{
    MAIL_LIST *new = (MAIL_LIST *)malloc(sizeof(MAIL_LIST));

    if(new == NULL)
    {
        printf("unable to alocate memory!\n");
        exit(EXIT_FAILURE);
    }

    strcpy(new->addr, addr);
    new->status = status;
    new->next = NULL;

    return new;
}

MAIL_LIST* find_node(MAIL_LIST** head, char* addr)
{    
    MAIL_LIST* tmp = *head;
    
    while (tmp != NULL)
    {
    	if (strcmp(tmp->addr, addr) == 0)
    	    break;
    	tmp = tmp->next;
    }
    
    return tmp;
}

void concat_node(MAIL_LIST **head, MAIL_LIST *new)
{
    MAIL_LIST *temp = *head;

    while (temp->next != NULL) {
    	temp = temp->next;
    }
    temp->next = new;
}

void add_node(MAIL_LIST **head, char* addr)
{
    if(*head == NULL)
        *head = make_node(addr, 'o');
     
    else
    {
    	MAIL_LIST* temp = *head;
    	
    	while (temp != NULL)
    	{
    	    if (strcmp(temp->addr, "0") == 0)
    	    {
    	    	strcpy(temp->addr, addr);
    	    	return;
    	    }
    	    temp = temp->next;
    	}
    	concat_node(head, make_node(addr, 'o'));
    }
}

void remove_node(MAIL_LIST** head, char* addr)
{
    MAIL_LIST* rem_node = find_node(head, addr);
    
    // trenutno ima u client-u ovo ali za svaki slucaj
    if (rem_node == NULL)
    {
    	printf("Node not found in list\n");
    	return;
    }
    
    strcpy(rem_node->addr, "0");
    if ((rem_node->next == NULL) && (rem_node != *head))
    {
    	MAIL_LIST* tmp = *head;
    	
    	while (tmp->next != rem_node)
    	    tmp = tmp->next;
    	tmp->next = NULL;
    	
    	free(rem_node);
    }
}

void output(MAIL_LIST *head)
{
    MAIL_LIST *temp = head;

    while (temp != NULL) 
    {
        printf("%s ", temp->addr);
        printf("%c\n", temp->status);
        temp = temp->next;
    }
}

//ovo je napravljeno slicno kao mail list, mozda moze da se generalise nekako i stavi u odvojen header
typedef struct Router_Node
{
    char ip[IP_LEN];
    int hopcount;
    struct Router_Node* next;
} Router_Table;

void rt_init(Router_Table **head)
{
    *head = NULL;
}

Router_Table *rt_make_node(char *ip, int hopcount)
{
    Router_Table *new = (Router_Table *)malloc(sizeof(Router_Table));

    if(new == NULL)
    {
        printf("unable to alocate memory!\n");
        exit(EXIT_FAILURE);
    }

    strcpy(new->ip, ip);
    new->hopcount = hopcount;
    new->next = NULL;

    return new;
}

void rt_concat_node(Router_Table **head, Router_Table *new)
{
    Router_Table *temp = *head;

    while (temp->next != NULL) {
    	temp = temp->next;
    }
    temp->next = new;
}

void rt_add_node(Router_Table **head, char* ip, int hopcount)
{
    if (hopcount > 20)
    {
    	printf("Hopcount too big\n");
    	return;
    }
    
    printf("Trying to add: %s %d\n", ip, hopcount);
    Router_Table* new_node = rt_make_node(ip, hopcount);

    // Ako je lista prazna ili novi ima manji hopcount
    if (*head == NULL || (*head)->hopcount > hopcount)
    {
        new_node->next = *head;
        *head = new_node;
        return;
    }

    Router_Table* current = *head;
    Router_Table* prev = NULL;

    // Prolazi kroz listu dok je sledeći čvor manji ili jednak
    while (current != NULL && current->hopcount <= hopcount)
    {
    	printf("Comparing '%s' with '%s'\n", current->ip, ip);
        // Ako već postoji čvor sa istim IP, preskoči/dodaj samo ako je bolji hopcount
        if (strcmp(current->ip, ip) == 0)
        {
            if (hopcount < current->hopcount)
            {
                current->hopcount = hopcount; // ažuriraj postojećeg
            }
            free(new_node); // već postoji — ne dodaj duplikat
            return;
        }

        prev = current;
        current = current->next;
    }

    // Ubaci novi čvor između prev i current
    new_node->next = current;
    prev->next = new_node;
}

Router_Table* rt_find_node(Router_Table** head, char* ip)
{    
    Router_Table* tmp = *head;
    
    while (tmp != NULL)
    {
    	printf("Comparing '%s' with '%s'\n", tmp->ip, ip);
    	if (strcmp(tmp->ip, ip) == 0)
    	    break;
    	tmp = tmp->next;
    }
    
    return tmp;
}

void rt_remove_node(Router_Table** head)
{
    if ((*head == NULL) || ((*head)->next == NULL))
    	return;
    	
    Router_Table* tmp = *head;
    
    while (tmp->next->next != NULL)
    	tmp = tmp->next;
    	
    free(tmp->next);
    tmp->next = NULL;
}

void rt_output(Router_Table *head)
{
    Router_Table *temp = head;

    while (temp != NULL) 
    {
        printf("%s ", temp->ip);
        printf("%d\n", temp->hopcount);
        temp = temp->next;
    }
}
