/* Danny Gletner
 * gush.c
 * © 2021 Daniel J Gletner
 * 
 * A custom shell for executing commands.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <linux/limits.h>

/****************
 * DECLARATIONS *
 ****************/

/** Global variables**/
char** history[20];
int history_index = 0, historyct = 0;
int redirect = 0, redirectIndex;
char* path[64];

/** Helper function declarations **/
void gushloop();
void gushbatch(char* file);
char* gushReadLine(void);
int gushExecute(char** args);
int gushLaunch(char** args);
char** gushSplitLine(char* line);
int gushError();
char** gushRedirect(char** args);
int gushIsHistoryShortcut(char** args);
char** gushGetHistoryCmd(char** args);
int gushContainsRedirect(char** args);
char* gushIsValidCmd(char* cmd);
char** gushSplitProcess(char** args, int pindex);
int gushGetPindex(char** args, int pindex);

int gush_num_builtins();

/**Built in shell command declarations**/
int gush_exit(char** args);
int gush_cd(char** args);
int gush_kill(char** args);
int gush_history(char** args);
int gush_pwd(char** args);
int gush_path(char** args);

/** Built in shell commands **/
char* builtin_names[] = {
	"exit",
	"cd",
	"kill",
	"history",
	"pwd",
	"path"
};

int (*builtin_func[]) (char**) = {
	&gush_exit,
	&gush_cd,
	&gush_kill,
	&gush_history,
	&gush_pwd,
	&gush_path
};


/*********************
 * Builtin functions *
 *********************/

/*gush_exit()
 * Exit the shell.
 *
 * @param args: the arguments
 */
int gush_exit(char** args){
	if(args[1] != NULL){
		gushError();
	       	return 1;
	}
	else
		exit(0);
}

/*gush_cd()
 * change to new directory specified by args
 *
 * @param args: the arguments
 */
int gush_cd(char** args){
	if(args[2] != NULL){
		gushError();
	}

	else if(chdir(args[1]) != 0){
		gushError();
	}
	return 1;
}

int gush_kill(char** args){
	char* murder = "";
	
	if(args[2] != NULL)
		gushError();
	else if(args[1] == NULL)
		gushError();
	else{
		strcat(murder, args[0]); //kill
		strcat(murder, " ");
		strcat(murder, args[1]);
		system(murder);
	}
	return 1;
}

/*gush_history()
 * Print the last 20 commands entered.
 *
 * @param args: the arguments
 */
int gush_history(char** args){
	if(args[1] != NULL){
		gushError();
		return 1;
	}

	int i=0, j=0, count = 0;
	if(historyct > 20){
		count = 20;
	}
	else count = historyct;

	while(i < count) {
		printf("%d: ", i+1);
		while(history[i][j] != NULL){
			printf("%s ", history[i][j]);
			j++;
		}
		printf("\n");
		i++;
		j=0;
	}
	return 1;
}

/*gush_pwd()
 * print the working directory
 *
 * @param args: the arguments
 */
int gush_pwd(char** args){
	
	if(args[1] != NULL)
		return gushError();

	char cwd[PATH_MAX];
	getcwd(cwd, sizeof(cwd));
	printf("%s\n", cwd);

	return 1;
}

/*gush_path()
 * Add path specified by user to be the path searched for commands
 *
 * @param args: the arguments
 */
int gush_path(char** args){
	int i = 1, j = 0;
	char* argscpy = malloc(sizeof(char*));
	
	//clear path
	while(path[j] != NULL){
		path[j] = NULL;
		j++;
	}
	j=0;
	
	//set new path(s)
	while(args[i] != NULL){
		strcpy(argscpy, args[i]);
		path[j] = malloc(sizeof(char*));
		strcpy(path[j], argscpy);
		i++;
		j++;
	}
	return 1;
}

/*returns number of builtins*/
int gush_num_builtins(){
	return sizeof(builtin_names)/sizeof(char*);
}

/*********************
 *  HELPER FUNCTIONS *
 *********************/

/*gushloop()
* Driver function for interactive mode
* prompts user for input, runs the command in a child process
*/
void gushloop(){
	char* line;
	char** args;
	char** pargs;
	char** pargs2;
	int pindex = 0;
	path[0] = "/bin";
	int run = 1;
	int redirect = 0;
	int rval;
	int inbackup = dup(STDIN_FILENO), outbackup = dup(STDOUT_FILENO);

	do{
		printf("gush> ");

		//parse
		line = gushReadLine();
		args = gushSplitLine(line);

		//history handling
		if(!(strcmp(args[0], "history") == 0)){
			if(!gushIsHistoryShortcut(args)){
				history[historyct%20] = args;
			}
			else{
				history[historyct%20] = args = gushGetHistoryCmd(args);
			}
			historyct++;
		}


		//parallel process handling
		pargs = gushSplitProcess(args, pindex);
		pindex = gushGetPindex(args, pindex);
		pindex++;
		while(args[pindex] != NULL){
			pargs2 = gushSplitProcess(args, pindex);
			pindex = gushGetPindex(args, pindex);
			pindex++;
			rval = fork();
			if(rval == 0){
				pargs = pargs2;
				break;
			}
		}

		redirect = gushContainsRedirect(pargs);

		pargs[0] = gushIsValidCmd(pargs[0]);

		//redirection handling
		if(redirect){
			char** newargs = gushRedirect(pargs);
			run = gushExecute(newargs);
			redirect = 0;
			dup2(inbackup, STDIN_FILENO);
			dup2(outbackup, STDOUT_FILENO);
		}
		else{
			run = gushExecute(pargs);
		}
		if(rval == 0) break;
		else wait(NULL);

		pindex = 0;
	} while(run);
	
	//janitor
	free(args);
	free(line);
	free(pargs);
}

/*gushbatch()
 * 	Driver function for batch mode
 * 	reads commands from a file and executes them
 */
void gushbatch(char* file){
	char* line;
	char** args;
	char** pargs;
	char** pargs2;
	int pindex = 0;
	path[0] = "/bin";
	redirect = 0;
	int inbackup = dup(STDIN_FILENO), outbackup = dup(STDOUT_FILENO);
	int run = 1;
	int rval;
	
	//open file
	int fd = open(file, O_RDONLY);
	if(fd == -1) {
		gushError();
		exit(EXIT_FAILURE);
	}

	dup2(fd, STDIN_FILENO);
	while(run){ 
		//parse
		line = gushReadLine();
		args = gushSplitLine(line);

		if(!(strcmp(args[0], "history") == 0) && args[1] == NULL){
			if(!gushIsHistoryShortcut(args)){
				history[historyct%20] = args;
			}
			else{
				history[historyct%20] = args = gushGetHistoryCmd(args);
			}
			historyct++;
		}

		//parallel handling
		pargs = gushSplitProcess(args, pindex);
		pindex = gushGetPindex(args, pindex);
		pindex++;
		while(args[pindex] != NULL){
			pargs2 = gushSplitProcess(args, pindex);
			pindex = gushGetPindex(args, pindex);
			pindex++;
			rval = fork();
			if(rval == 0){
				pargs = pargs2;
				break;
			}
		}


		//cmd validity check
		redirect = gushContainsRedirect(pargs);
		pargs[0] = gushIsValidCmd(pargs[0]);

		//redirection handling
		if(redirect){
			char** newargs = gushRedirect(pargs);
			run = gushExecute(newargs);
			redirect = 0;
			dup2(inbackup, STDIN_FILENO);
			dup2(outbackup, STDOUT_FILENO);
		}
		else
			run = gushExecute(pargs);
		if(rval == 0) break;
		else wait(NULL);
		
		pindex = 0;
	}
	free(args);
	free(pargs);
	free(line);
	exit(EXIT_SUCCESS);
}

/*gushReadLine()
 * 	reads a line of input from stdin
 * 	returns the line
 */
char* gushReadLine(void){

	char* line = NULL;
	ssize_t lineSize = 0;

	if(getline(&line, &lineSize, stdin) == -1){
		if(feof(stdin)){
			exit(EXIT_SUCCESS); //reached eof
		}
		else {
			gushError();
			exit(EXIT_FAILURE);
		}
	}
	return line;
}

#define BUFSIZE 64
#define DELIMS " \n\t\r\a"
/*gushSplitLine()
 * parses a line of aruments
 * returns the args array
 * 
 * @param line: the line to split
 */
char** gushSplitLine(char* line){
	int bufsize = BUFSIZE, pos = 0;
	char** tokens = malloc(bufsize * sizeof(char*));
	char* token;

	if(!tokens){
		gushError();
		exit(EXIT_FAILURE);
	}

	token = strtok(line, DELIMS);
	while(token != NULL){
		tokens[pos] = token;
		pos++;

		if(pos > bufsize){
			bufsize += BUFSIZE;
			tokens = realloc(tokens, bufsize * sizeof(char*));
			if(!tokens){
				gushError();
				exit(EXIT_FAILURE);
			}
		}
		token = strtok(NULL, DELIMS);
	}
	tokens[pos] = NULL;
	if(tokens[0] == NULL){
		tokens[0] = "deadcmd";
	}
	return tokens;
}

/*gushSplitProcess()
 * parses args for parallel processing
 * returns the next process
 * 
 * @param args: the args array
 * @param index: index of args to parse from
 */
char** gushSplitProcess(char** args, int index){

	int i = 0;
	char** newargs = malloc(64*sizeof(char*)); //need to add realloc
	while(args[index] != NULL){
		if(strcmp(args[index],"&") != 0){
			newargs[i] = malloc(sizeof(char));
			strcpy(newargs[i], args[index]);
			index++;
			i++;
		}
		else break;
	}
	newargs[i] = NULL;
	return newargs;
}
/*gushGetPindex()
 * returns index where a '&' delimiter was found
 * indicating a new parallel process
 *
 * @param args: the arguments to parse
 * @param pindex: the current index
 */
int gushGetPindex(char** args, int pindex){
	while(args[pindex] != NULL){
		if(strcmp(args[pindex], "&") == 0)
			break;
		pindex++;
	}
	return pindex;
}

/*gushExecute()
 * takes in a list of arguments and calls the command including builtins
 *
 * @param args: the arguments
 */
int gushExecute(char** args){

	int i;
	if(args[0] == NULL) return 1; //empty command

	for(i = 0; i < gush_num_builtins(); i++){
		if(strcmp(args[0], builtin_names[i]) == 0){
			return (*builtin_func[i])(args);
		}
	}
	return gushLaunch(args);	
}

/*gushLaunch()
 * executes a non builtin command
 *
 * @param args: the arguments
 */
int gushLaunch(char** args){

	pid_t pid, wpid;
	int status;

	pid = fork();
	if(pid == 0) { //child
		if(execve(args[0], args, NULL) == -1) {
			return gushError(); //execve fail
		}
	} 
	else { //parent
		do {
			wpid = waitpid(pid, &status, WUNTRACED);
		} while(!WIFEXITED(status) && !WIFSIGNALED(status));
	}
	return 1;

}

/*gushRedirect()
 * reassigns file descriptors according to redirect symbol found in args
 * returns a new arguments list not containing redirection symbol
 *
 * @param args: the arguments
 */
char** gushRedirect(char** args){
	int fd, bufsize = BUFSIZE, pos = 0;
	char** newargs = malloc(redirectIndex*sizeof(char*));
	
	if(strcmp(args[redirectIndex], "<") == 0){
		fd = open(args[redirectIndex+1], O_RDONLY);
		if(fd == -1){
			gushError();
			return args; //do nothing
		}
		else{
			dup2(fd, STDIN_FILENO);
		}
	}
	else{
		fd = open(args[redirectIndex+1], O_CREAT | O_WRONLY | O_TRUNC, 0600);
		if(fd == -1){
			gushError();
			exit(EXIT_FAILURE);
		}
		dup2(fd, STDOUT_FILENO);
	}
	int i = 0, j = 0;
	while(i < redirectIndex){
		newargs[i] = args[i];
		i++;
	}
	newargs[i] = NULL;
	return newargs;
}

/*gushContainsRedirect()
 * searches an args list for a redirection symbol
 * returns 1 if found, else 0
 *
 * @param args: the arguments
 */
int gushContainsRedirect(char** args){
	int i = 0;

	while(args[i] != NULL){
		if(strcmp(args[i], ">") == 0 || strcmp(args[i], "<") == 0){
			redirectIndex = i;
			return 1;
		}
		i++;
	}
	return 0;
}

/*gushIsHistoryShortcut()
 * parses args list for a history shortcut symbol
 * returns 1 if found, else 0
 *
 * @param args: the arguments
 */
int gushIsHistoryShortcut(char** args){
	
	if(args[0][0] == '!'){
		return 1;
	}
	return 0;

}

/*gushGetHistoryCmd()
 * parses history shortcut number from args list
 * returns the command associated with that position in program history
 *
 * @param args: the arguments
 */
char** gushGetHistoryCmd(char** args){
	int i = 0;
	char* shortcut = malloc(3*sizeof(char));
	int newargsSize = BUFSIZE;
	char** newargs = malloc(newargsSize*sizeof(char*));
	char c; 

	//parse the number from args[0]
	while(i < 2){
		c = args[0][i+1];
		if(c != '\0' && c != '\n'){
			shortcut[i] = c;
		}
		i++;
	}
	//find cmd in history
	newargs = history[atoi(shortcut)-1];

	return newargs;
}
/*gushIsValidCmd()
 * checks if a command is valid
 * searches in all paths within path[]
 *
 * @param cmd: the command
 */
char* gushIsValidCmd(char* cmd){
	char* newcmd = malloc(strlen(cmd)*2*sizeof(char));
	char* tmp = malloc(strlen(cmd)*sizeof(char));
	int i = 0;

	if(access(cmd, X_OK) == 0)
		return cmd;

	while(path[i] != NULL){
		strcat(tmp, path[i]);
		strcat(tmp, "/");
		strcat(tmp, cmd);
		if(access(tmp, X_OK) == 0){
			strcpy(newcmd, tmp);
			return newcmd;
		}
		else strcpy(tmp, "");
	        
		i++;
	}
	return cmd;
}	


/*gushError()
 * outputs an error message
 */
int gushError(void){
	
	char error_message[30] = "An error has occurred\n";
	write(STDERR_FILENO, error_message, strlen(error_message));
	return 0;
}


/** main **/
int main(int argc, char* argv[]){

	if(argc > 2)
		return gushError();
	else if(argc == 2)
	       	gushbatch(argv[1]);
	else 
		gushloop();

	return EXIT_SUCCESS;
}
