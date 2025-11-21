#include "user.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Load user accounts from a file
 * @param filename The name of the file containing user accounts
 * @param users Array to store loaded User structs
 * @param max_users Maximum number of users to load
 * @return int Number of users loaded
 */

int loadAccounts(const char *filename, User users[], int max_users)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Cannot open account file");
        return 0;
    }

    int count = 0;
    char line[1024];
    char temp_username[MAX_NAME];
    int status;

    while (count < max_users && fgets(line, sizeof(line), file))
    {
        // Read username and status
        if (sscanf(line, "%511s %d", temp_username, &status) == 2)
        {
            strncpy(users[count].name, temp_username, MAX_NAME - 1);
            users[count].name[MAX_NAME - 1] = '\0';
            users[count].status = status;
            count++;
        }
    }

    fclose(file);
    printf("Loaded %d accounts\n", count);
    return count;
}

/**
 * @brief Find a user by username
 * @param username The username to search for
 * @param users Array of User structs
 * @param count Number of users in the array
 * @return int Index of the user in the array, or -1 if not found
 */

int findUser(const char *username, User users[], int count)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(username, users[i].name) == 0)
            return i;
    }
    return -1;
}
