#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>


// arglist - a list of char* arguments (words) provided by the user
// it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
// RETURNS - 1 if should continue, 0 otherwise

int parse_no_wait(int count, char** arglist){
	char **temp = malloc(sizeof(char*) * count -1);
	if (temp == NULL) {
		perror("malloc");
	return 0;
	}
	for (int i = 0 ; i < count - 1 ; i++){
		temp[i] = arglist[i];
	}

	temp[count-1] = NULL;

	pid_t pid = fork(); 
	if (pid < 0){//failed
		perror("fork");
		return 0;
	}
	if (pid == 0){ //child
		execvp(temp[0] , temp);
		perror("execvp");
		free(temp);
        exit(EXIT_FAILURE);
	}
	else{
		free(temp);
		return 1;
	}
};

int parse_redirecting(int count , char** arglist){
	int fd = -1;
	if (strcmp(arglist[count - 2], "<") == 0) {
        fd = open(arglist[count - 1], O_RDONLY);
        if (fd == -1) {
            perror("open");
            return 0;
        }

        int pid = fork();
        if (pid < 0) {
            perror("fork");
            close(fd);
            return 0;
        }

        if (pid == 0) { // child
			signal(SIGINT, SIG_DFL);
            if (dup2(fd, STDIN_FILENO) == -1) {
                perror("dup2");
                close(fd);
                exit(EXIT_FAILURE);
            }
            close(fd);
            char **temp = malloc(sizeof(char*) * (count - 1));
			if (temp == NULL) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}

            for (int i = 0; i < count - 2; i++) {
                temp[i] = arglist[i];
            }
            temp[count - 2] = NULL;
            execvp(temp[0], temp);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else { // parent
            close(fd);
        	waitpid(pid, NULL, 0);
            return 1;
        }
    } else if (strcmp(arglist[count - 2], ">") == 0) {
        fd = open(arglist[count - 1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd == -1) {
            perror("open");
            return 0;
        }

        int pid = fork();
        if (pid < 0) {
            perror("fork");
            close(fd);
            return 0;
        }

        if (pid == 0) { // child
			signal(SIGINT, SIG_DFL);
            if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(fd);
                exit(EXIT_FAILURE);
            }
            close(fd);
            char **temp = malloc(sizeof(char*) * (count - 1));
			if (temp == NULL) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}
            for (int i = 0; i < count - 2; i++) {
                temp[i] = arglist[i];
            }
            temp[count - 2] = NULL;
            execvp(temp[0], temp);
            perror("execvp");
			free(temp);
            exit(EXIT_FAILURE);
        } else { //parent
            close(fd);
            waitpid(pid, NULL, 0);
            return 1;
        }
	}
	return 0;
};

int parse_piping(int count , char** arglist)
{
    int num_pipes = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], "|") == 0) {
            num_pipes++;
        }
    }

	if (num_pipes > 9){
		perror("pipes");
		return 1;
	}

	int pipefds[num_pipes][2];

	for (int i = 0; i < num_pipes; i++) {
		int p = pipe(pipefds[i]);
        if (p == -1) {
            perror("pipe");
            return 0;
        }
    }

	int start = 0;
	pid_t child_pids[num_pipes + 1];

	for (int i = 0 ; i<= num_pipes; i++){

		int end = count;
		for(int j = start ; j < count ; j++){
			if (strcmp(arglist[j], "|") == 0) {
                end = j;
                break;
            }
		}

		pid_t pid = fork();
		
		if (pid == -1){ //failed
			perror("fork");
            return 0;
		}
		else if (pid == 0){ //child
			signal(SIGINT, SIG_DFL);
			char **command = malloc(sizeof(char*) * (end - start + 1));
			if (command == NULL) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}
			for (int j = 0; j < end - start; j++) {
                command[j] = arglist[start + j];
            }
            command[end - start] = NULL;

			if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO); // Read from previous pipe
            }

			if (i < num_pipes) {
                dup2(pipefds[i][1], STDOUT_FILENO); // Write to next pipe
            }

			for (int k = 0; k < num_pipes; k++) {
                close(pipefds[k][0]);
                close(pipefds[k][1]);
            }
			
			execvp(command[0], command);
			perror("execvp");
            exit(EXIT_FAILURE);


		}
		else{ //parent
			child_pids[i] = pid;
			if (i < num_pipes) {
                close(pipefds[i][1]);
            }
			if (i > 0) {
                close(pipefds[i - 1][0]);
            }
            start = end + 1;
		}
	}
	for (int i = 0; i <= num_pipes; i++) {
		waitpid(child_pids[i], NULL, 0);
	}
	return 1;
};

int process_arglist(int count, char** arglist)
{
	int out = 1;
	int regular = 1;
	for (int i = 0 ; i < count ; i++){
		if (strcmp(arglist[i],"|") == 0){
			out = parse_piping(count,arglist);
			regular = 0;
			break;
		}
	}
	
	if (count > 2 && (strcmp(arglist[count - 2], "<") == 0 || strcmp(arglist[count - 2], ">") == 0)){
		out = parse_redirecting(count,arglist);
		regular = 0;
	}

	if (strcmp(arglist[count-1] , "&") == 0){
		out = parse_no_wait(count,arglist);
		regular = 0;
	}

	if (regular){
		pid_t pid = fork();
		if (pid < 0){//failed
			perror("fork");
			return(0);
		}
		if (pid == 0){ //child
			signal(SIGINT, SIG_DFL);
			execvp(arglist[0] , arglist);
			perror("execvp");
		}
		else {
			waitpid(pid,NULL,0);
		}
	}
	return out;
};

void sigint_handler(int sigID){
	pid_t child_pid = 1;
	while ((child_pid = waitpid(-1, NULL, WNOHANG)) > 0){}
};


int prepare(void) {
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigint_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_NOCLDWAIT | SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction (SIGCHLD)");
        return 1;
    }

    struct sigaction sa_int;
    sa_int.sa_handler = SIG_IGN;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction (SIGINT)");
        return 1;
    }
    return 0;
}


int finalize(void){
	return 0;
};

/*(int main(void)
{
	if (prepare() != 0)
		exit(1);
	
	while (1)
	{
		char** arglist = NULL;
		char* line = NULL;
		size_t size;
		int count = 0;

		if (getline(&line, &size, stdin) == -1) {
			free(line);
			break;
		}
    
		arglist = (char**) malloc(sizeof(char*));
		if (arglist == NULL) {
			printf("malloc failed: %s\n", strerror(errno));
			exit(1);
		}
		arglist[0] = strtok(line, " \t\n");
    
		while (arglist[count] != NULL) {
			++count;
			arglist = (char**) realloc(arglist, sizeof(char*) * (count + 1));
			if (arglist == NULL) {
				printf("realloc failed: %s\n", strerror(errno));
				exit(1);
			}
      
			arglist[count] = strtok(NULL, " \t\n");
		}
    
		if (count != 0) {
			if (!process_arglist(count, arglist)) {
				free(line);
				free(arglist);
				break;
			}
		}
    
		free(line);
		free(arglist);
	}
	
	if (finalize() != 0)
		exit(1);

	return 0;
}*/