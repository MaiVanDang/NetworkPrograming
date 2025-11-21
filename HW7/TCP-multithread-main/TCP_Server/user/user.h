#ifndef USER_H
#define USER_H

#define MAX_NAME 512
#define MAX_USERS 11000

typedef struct
{
    char name[MAX_NAME];
    int status; // 0 = blocked, 1 = active
} User;

int loadAccounts(const char *filename, User users[], int max_users);
int findUser(const char *username, User users[], int count);

#endif
