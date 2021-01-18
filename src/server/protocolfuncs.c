#include "../../include/protocolfuncs.h"

/*
    Reading pattern USER
    sem_wait(&usermutex);
    usercnt++;
    if (usercnt == 1)
        sem_wait(&userw);
    sem_post(&usermutex);
    
    Read something
    
    sem_wait(&usermutex);
    usercnt--;
    if (usercnt == 0)
        sem_post(&userw);
    sem_post(&usermutex);


    Writing pattern
    sem_wait(&userw);
    Write some shit
    sem_post(&userw);
*/

/*
    Reading pattern ROOM
    sem_wait(&roommutex);
    roomcnt++;
    if (roomcnt == 1)
        sem_wait(&roomw);
    sem_post(&roommutex);

    //Read something
    
    sem_wait(&roommutex);
    roomcnt--;
    if (roomcnt == 0)
        sem_post(&roomw);
    sem_post(&roommutex);


    Writing pattern
    sem_wait(&roomw);
    //Write some shit
    sem_post(&roomw);
*/

void protocolfuncsinit(void)
{
    sem_init(&usermutex, 0, 1);
    sem_init(&userw, 0, 1);
    sem_init(&roommutex, 0, 1);
    sem_init(&roomw, 0, 1);
    usercnt = 0;
    roomcnt = 0;
}

void login(user *newuser)
{
    sem_wait(&userw);
    insertRear(&userlist, (void *)newuser);
    sem_post(&userw);
}

void logout(user *olduser, bool terminated)
{

    //WRITE USER
    sem_wait(&userw);

    // Find olduser in userlist and create index
    int userindex = 0;
    node_t *iter = userlist.head;
    while (iter)
    {
        if (strcmp(olduser->name, ((user *)(iter->value))->name) == 0)
        {
            break;
        }
        userindex++;
        iter = iter->next;
    }

    //WRITE ROOM
    sem_wait(&roomw);
    //loop
    int n = 0;

    //iterating through all rooms
    iter = roomlist.head;
    petr_header header;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    room *rm = NULL;
    user *curruser = NULL;
    while (iter)
    {
        //iterate each room
        rm = ((room *)(iter->value));
        node_t *iter2 = rm->userlist.head;
        int userinroom = 0;
        // Iterate through each user in room
        while (iter2)
        {
            curruser = ((user *)(iter2->value));
            //if user in room
            if (strcmp(olduser->name, curruser->name) == 0)
            {
                // Remove user from rooms userlist
                removeByIndex(&(rm->userlist), userinroom);
                if (strcmp(olduser->name, rm->creator->name) == 0)
                {
                    // Iterate through each user in the room olduser is also in
                    node_t *iter3 = rm->userlist.head;
                    while (iter3)
                    {
                        curruser = ((user *)(iter3->value));
                        //send CLOSED
                        header.msg_len = strlen(rm->name) + 1;
                        strcpy(buffer, rm->name);
                        header.msg_type = RMCLOSED;
                        wr_msg(curruser->connfd, &header, buffer);

                        iter3 = iter3->next;
                    }
                    // Delete rooms userlist
                    deleteList(&(rm->userlist));
                    // Delete room - find index then remove by index then free
                    node_t *roomiter = roomlist.head;
                    int roomindex = 0;
                    while (roomiter)
                    {
                        if (strcmp(((room *)(roomiter->value))->name, rm->name) == 0)
                        {
                            break;
                        }
                        roomindex++;
                        roomiter = roomiter->next;
                    }
                    removeByIndex(&roomlist, roomindex);
                    free(rm);
                    //IF CREATOR,send all users from list CLOSED loop
                    //then delete the rooms userlist, then delete room
                }
                break;
            }
            iter2 = iter2->next;
            userinroom++;
        }

        //USERW

        iter = iter->next;
    }

    sem_post(&roomw);

    //WRITE USER
    // Remove client from userlist
    removeByIndex(&userlist, userindex);

    //send ok
    if (!terminated)
    {
        header.msg_len = 0;
        header.msg_type = OK;
        bzero(&buffer, sizeof(buffer));
        wr_msg(olduser->connfd, &header, buffer);
    }
    close(olduser->connfd);
    free(olduser);
    sem_post(&userw);
}

void rmleave(user *newuser, char *roomname)
{
    int n = 0;
    //find room ROOMW
    sem_wait(&roomw);
    node_t *iter = roomlist.head;
    room *rm = NULL;
    while (iter)
    {
        if (strcmp(((room *)iter->value)->name, roomname) == 0)
        {
            rm = ((room *)(iter->value));
            break;
        }
        iter = iter->next;
    }

    //find user in room USERM
    sem_wait(&usermutex);
    usercnt++;
    if (usercnt == 1)
        sem_wait(&userw);
    sem_post(&usermutex);

    node_t *useriter = rm->userlist.head;
    while (useriter)
    {
        if (strcmp(((user *)useriter->value)->name, newuser->name) == 0)
        {
            break;
        }
        useriter = useriter->next;
        n++;
    }

    //remove user if they are in room WRITING ROOM
    if (useriter != NULL)
    {
        removeByIndex(&rm->userlist, n);
    }

    sem_wait(&usermutex);
    usercnt--;
    if (usercnt == 0)
        sem_post(&userw);
    sem_post(&usermutex);

    //end READW
    sem_post(&roomw);

    //send ok to this person
    petr_header header;
    header.msg_len = 0;
    header.msg_type = OK;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    wr_msg(newuser->connfd, &header, buffer);
}

void rmcreate(user *usercreator, char *newroomname)
{

    //ROOMW
    sem_wait(&roomw);

    //create room w malloc
    room *newroom = malloc(sizeof(room));

    //set roomname
    //newroom->name = (char*)malloc() ;
    strcpy(newroom->name, newroomname);
    //make user creator
    newroom->creator = usercreator;

    //create userlist, static list_t
    List_t roomuserlist;
    roomuserlist.head = NULL;
    roomuserlist.length = 0;

    //add creator to roomuserlist
    insertRear(&roomuserlist, (void *)usercreator);

    newroom->userlist = roomuserlist;

    //ROOMW END
    //add room to roomlist
    insertRear(&roomlist, (void *)newroom);
    sem_post(&roomw);

    //send ok to this person
    petr_header header;
    header.msg_len = 0;
    header.msg_type = OK;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    wr_msg(usercreator->connfd, &header, buffer);
}

void rmdelete(char *oldroom)
{

    //USERW
    sem_wait(&userw);
    // RoOM W
    sem_wait(&roomw);

    // Index of creator in room userlist
    int n = 0;
    // Room to be deleted
    room *deleteroom = NULL;
    // Find room in roomlist
    node_t *iter = roomlist.head;
    while (iter)
    {
        if (strcmp(((room *)(iter->value))->name, oldroom) == 0)
        {
            deleteroom = ((room *)(iter->value));
            break;
        }
        iter = iter->next;
    }
    // Find creators index in rooms userlist
    iter = deleteroom->userlist.head;
    while (iter)
    {
        if (strcmp(((user *)(iter->value))->name, deleteroom->creator->name) == 0)
        {
            break;
        }
        n++;
        iter = iter->next;
    }
    // Delete creator from rooms userlist
    removeByIndex(&(deleteroom->userlist), n);
    // Send rmclosed to each other user in the rooms userlist
    petr_header header;
    iter = deleteroom->userlist.head;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    while (iter)
    {
        header.msg_len = strlen(oldroom) + 1;
        header.msg_type = RMCLOSED;
        strcpy(buffer, oldroom);
        wr_msg(((user *)(iter->value))->connfd, &header, buffer);
        iter = iter->next;
    }
    // Delete room's userlist
    deleteList(&(deleteroom->userlist));
    // Find index for room in roomlist
    n = 0;
    iter = roomlist.head;
    while (iter)
    {
        if (strcmp(((room *)(iter->value))->name, oldroom) == 0)
        {
            break;
        }
        n++;
        iter = iter->next;
    }
    // Remove room from roomlist
    removeByIndex(&roomlist, n);
    // free the room
    user *creat = deleteroom->creator;
    free(deleteroom);
    // Write OK to creator
    header.msg_len = 0;
    header.msg_type = OK;
    bzero(&buffer, sizeof(buffer));
    wr_msg(creat->connfd, &header, buffer);
    // Room write mutex
    sem_post(&roomw);
    // User read mutex
    sem_post(&userw);
}

void rmlist(user *thisuser)
{

    //if rmlist length 0, send RMLIST lenght 0
    petr_header header;
    header.msg_len = 0; //can change
    header.msg_type = RMLIST;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));

    //ROOM READ START
    sem_wait(&roommutex);
    roomcnt++;
    if (roomcnt == 1)
        sem_wait(&roomw);
    sem_post(&roommutex);

    //USER READ START
    sem_wait(&usermutex);
    usercnt++;
    if (usercnt == 1)
        sem_wait(&userw);
    sem_post(&usermutex);

    if (roomlist.length == 0)
    {
        wr_msg(thisuser->connfd, &header, buffer);
    }
    else
    {

        //iterate through roomlist, add name
        node_t *iter = roomlist.head;

        //char roomnamex[strlen( ((room*)(iter->value))->name )];
        // memcpy(roomnamex, ((room*)(iter->value))->name, strlen(((room*)(iter->value))->name));
        while (iter)
        {
            strcat(buffer, ((room *)(iter->value))->name);
            strcat(buffer, ": ");
            node_t *iter2 = ((room *)(iter->value))->userlist.head;
            while (iter2)
            {
                strcat(buffer, ((user *)(iter2->value))->name);
                if (iter2->next != NULL)
                {
                    strcat(buffer, ",");
                }
                iter2 = iter2->next;
            }
            strcat(buffer, "\n");
            iter = iter->next;
        }
        //for each room, iterate through roomuserlist
        // USER READ END
        header.msg_len = strlen(buffer) + 1;
        wr_msg(thisuser->connfd, &header, buffer);
    }
    //USER READ END
    sem_wait(&usermutex);
    usercnt--;
    if (usercnt == 0)
        sem_post(&userw);
    sem_post(&usermutex);

    //ROOM READ END
    sem_wait(&roommutex);
    roomcnt--;
    if (roomcnt == 0)
        sem_post(&roomw);
    sem_post(&roommutex);
}

void rmjoin(user *newuser, char *roomname)
{
    // Room write mutex
    sem_wait(&roomw);
    // Find room with name roomname
    room *rm = NULL;
    node_t *iter = roomlist.head;
    while (iter)
    {
        if (strcmp(((room *)iter->value)->name, roomname) == 0)
        {
            rm = ((room *)iter->value);
            break;
        }
        iter = iter->next;
    }
    // Add newuser to rm->userlist
    insertRear(&rm->userlist, (void *)newuser);
    // Room write mutex
    sem_post(&roomw);
    petr_header header;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    header.msg_len = 0;
    header.msg_type = OK;
    wr_msg(newuser->connfd, &header, buffer);
}

void rmsend(user *newuser, char *roomname, char *roommsg)
{
    /* Send RMRECV <roomname>\r\n<from_username>\r\n<message> and OK to sender */
    // Room read mutex
    sem_wait(&roommutex);
    roomcnt++;
    if (roomcnt == 1)
        sem_wait(&roomw);
    sem_post(&roommutex);

    // Find room
    room *rm = NULL;
    node_t *iter = roomlist.head;
    while (iter)
    {
        if (strcmp(((room *)iter->value)->name, roomname) == 0)
        {
            rm = ((room *)iter->value);
            break;
        }
        iter = iter->next;
    }
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    strcat(buffer, roomname);
    strcat(buffer, "\r\n");
    strcat(buffer, newuser->name);
    strcat(buffer, "\r\n");
    strcat(buffer, roommsg);
    petr_header header;
    header.msg_type = RMRECV;
    header.msg_len = strlen(buffer) + 1;

    //Send msg to every user in room, except sender
    iter = rm->userlist.head;
    while (iter)
    {
        if (strcmp(newuser->name, ((user *)(iter->value))->name) != 0)
        {
            wr_msg(((user *)(iter->value))->connfd, &header, buffer);
        }
        iter = iter->next;
    }

    //Send ok to sender
    header.msg_type = OK;
    header.msg_len = 0;
    bzero(&buffer, sizeof(buffer));
    wr_msg(newuser->connfd, &header, buffer);

    // Room read mutex end
    sem_wait(&roommutex);
    roomcnt--;
    if (roomcnt == 0)
        sem_post(&roomw);
    sem_post(&roommutex);
}

void usrsend(user *sender, char *recipient, char *usermsg)
{
    /* Send USRRECV <from_username>\r\n<message> to recipient and OK to sender */
    // User read mutex
    sem_wait(&usermutex);
    usercnt++;
    if (usercnt == 1)
        sem_wait(&userw);
    sem_post(&usermutex);

    // Find recipient user struct
    user *recv = NULL;
    node_t *iter = userlist.head;
    while (iter)
    {
        if (strcmp(((user *)iter->value)->name, recipient) == 0)
        {
            recv = ((user *)iter->value);
            break;
        }
        iter = iter->next;
    }
    // Create and send message to recipient
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));
    strcat(buffer, sender->name);
    strcat(buffer, "\r\n");
    strcat(buffer, usermsg);
    petr_header header;
    header.msg_type = USRRECV;
    header.msg_len = strlen(buffer) + 1;
    wr_msg(recv->connfd, &header, buffer);
    // Send OK to sender
    header.msg_type = OK;
    header.msg_len = 0;
    bzero(&buffer, sizeof(buffer));
    wr_msg(sender->connfd, &header, buffer);

    // User read mutex end
    sem_wait(&usermutex);
    usercnt--;
    if (usercnt == 0)
        sem_post(&userw);
    sem_post(&usermutex);
}

void usrlist(user *thisuser)
{

    //if usrlist length 0, send length 0
    petr_header header;
    header.msg_len = 0; //can change
    header.msg_type = USRLIST;
    char buffer[1024];
    bzero(&buffer, sizeof(buffer));

    //USER R START
    sem_wait(&usermutex);
    usercnt++;
    if (usercnt == 1)
        sem_wait(&userw);
    sem_post(&usermutex);

    if (userlist.length == 1)
    {
        wr_msg(thisuser->connfd, &header, buffer);
    }
    else
    {

        //iterate through userlist, add name
        node_t *iter = userlist.head;

        while (iter)
        {
            if (strcmp((((user *)(iter->value))->name), thisuser->name) != 0)
            {
                strcat(buffer, ((user *)(iter->value))->name);
                strcat(buffer, "\n");
            }
            iter = iter->next;
        }

        header.msg_len = strlen(buffer) + 1;
        wr_msg(thisuser->connfd, &header, buffer);
    }
    // USER READ END
    sem_wait(&usermutex);
    usercnt--;
    if (usercnt == 0)
        sem_post(&userw);
    sem_post(&usermutex);
}
