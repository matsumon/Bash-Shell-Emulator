#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

//Global Variables that cover:
//number of forks created, an array to hold all the background process pids,
//the parent process's pid, a value that holds whether a process is supposed
//to be in the background or foreground, the id of the fork, the pid of the
//most recent process, a pointer to a number that has the exit method, and a 
//variable that corresponds to whether the shell accepts background process.

int forkCount = 0;
pid_t holder[512];
int parent;
int priority;
pid_t id = -100;
int recent_process = -10;
int * exitMethod;
volatile sig_atomic_t background_toggle=0;

void getInput();
void handleInput(char *);
void shellExit();
void shellCD(char *);
int expansion(char*);
void shellStatus();
void childStatus(int *);
int findAnd(char*);
void execute(char *, int);
void redirection(char *, int);
void signalStop(int);
void signalTerm(int);
int checkForBlank(char *);

int main()
{
//Two signal handlers that control the shells response to SIGINT and SIGTSTOP
//I wish i had set the flag to sa_restart so that getline would restart rather
//than needing the stdin to be cleared.
	struct sigaction sigStpHandler ={0};
	sigStpHandler.sa_handler=signalStop;
	sigemptyset(&sigStpHandler.sa_mask);
	sigStpHandler.sa_flags=0;
	sigaction(SIGTSTP,&sigStpHandler,NULL);
	struct sigaction sigIntHandler={0};
	sigIntHandler.sa_handler=SIG_IGN;
	sigemptyset(&sigIntHandler.sa_mask);
	sigaddset(&sigIntHandler.sa_mask,SIGINT);
	sigaction(SIGINT,&sigIntHandler,NULL);
//Setting the holder to 0 so that garbage values dont pop up.
//setting the pid of the parent
	memset(holder,0,sizeof(holder[0]));
	parent = getpid();
	while(1)
	{
		int i = 0;
//checking to make sure that process is the parent
		if(parent == getpid())
		{
//setting the childexit method and child pid to values that wont trigger the 
//conditionals down below. If the waitpid returns a zombie then the pid of 
//the child is printed.
			int * childExitMethod=0;
			int childpid = -1;
			childpid=waitpid(-1,&childExitMethod,WNOHANG);
			if(childpid > 0)
			{
				printf("Child PID: %d has finished. \n ",childpid);
				fflush(stdout);
				childStatus(childExitMethod);
			}	
//forced to use clearerr to clear the input filestream as catching a signal
//will put a EOF in the stream making getline fail over and over
			clearerr(stdin);
			fflush(stdout);
			printf(": ");
			fflush(stdout);
			clearerr(stdin);
			getInput();
//tells the user that child process is created
			if(id > 0 && priority == 0)
			{
				printf("Child PID: %d was created. \n",id);
				fflush(stdout);
			}
		}
//breaks the process if it is not the parent
		else
		{
			break;
		}
	}
	return 0;
}

//input nothing
//output nothing
//purpose: gets input and then sends it to the handleInput function

void getInput()
{
	char * input = NULL;
	size_t size = 2048;
	getline(&input,&size,stdin);
	handleInput(input);
}

//input: accepts a character string
//output: nothing
//purpose: Checks input for the inbuilt commands: 1) cd, 2) status, 3) exit.
//input is also being checked for # and blank spaces. The rest of the possible
//inputs are sent to be searched in the bash shell

void handleInput(char * input)
{
//returns immediately if input has nothing in it
	if(input==NULL)
	{
		return;
	}
	char inputCopy [2048];
	char inputCopy2 [2048];
	int check =1;
	strcpy(inputCopy2,input);
//checks if input only has spaces in it and returns if it does
	if(checkForBlank(inputCopy2) == 0)
	{
		return;
	}
//expands $$ to pid
	while(check==1)
	{
		check =expansion(inputCopy2);
		fflush(stdout);
	}
	strcpy(inputCopy,inputCopy2);
//gets rid of new line that getline stuck there 
	inputCopy[strlen(inputCopy)-1]=0;
	free(input);
	input = NULL;
	char * exit1 = "exit";
	char * exit2 = "exit &";
	char * status1 = "status";
	char * status2 = "status &";
//checks if first character is # and retetruns if it does.
//this would have been better placed at the top because things like shell 
//expansion wouldnt have needed to occur and would have resulted in a faster
//program.
	if(inputCopy[0] == '#' || strlen(inputCopy) == 0)
	{
		return;
	}
//checks and looks to see if exit was called by the user
	else if((strcmp(inputCopy,exit1)==0)||strcmp(inputCopy,exit2)==0)
	{
		shellExit();
	}
//checks and looks for cd 
	else if((inputCopy[0] == 'c' && inputCopy[1] == 'd') &&(inputCopy[2] == ' ' || inputCopy[2] == '\0'))
	{
		shellCD(inputCopy);
	}
//check and looks for status of the shell
	else if((strcmp(inputCopy,status1)==0)||strcmp(inputCopy,status2)==0)
	{
		shellStatus();
	}
	else 
	{
//makes sure that only the parent process can run the following conditional
		if(parent == getpid())
		{
//sets whether process will run in the foreground or background
			priority = findAnd(inputCopy); 	
//checks to see whether foreground mode is on
			if(background_toggle == 1)
			{
				priority = 1;
			}
//makes sure that the program isnt fork bombing the os1 server
			if(forkCount < 100)
			{
//increases global variable fork count, begins fork
				forkCount++;
				id = fork();	
				int current = getpid();
				if(priority == 1 && id )
				{
					recent_process = id;
					if(priority == 1)
					{
//waits on the foreground process and if terminated by a signal prints the signal
//number
						waitpid(id ,&exitMethod, 0);
						if(WIFSIGNALED(exitMethod) !=0 )
						{
							int z;
							printf("terminated by signal %d\n",WTERMSIG(exitMethod));
							fflush(stdout);
						}
					}

				}
//background process pids are stored in an array
				int i = 0;
				if( priority == 0 && parent == current)
				{
					while(holder[i] != 0)
					{
						i++;
					}
					holder[i] = id;
				}
			}
//makes sure the number of forks doesnt exceed a 100
			else
			{
				printf("Too many forks");
				fflush(stdout);
				return;
			}
//starts the exec call and if it returns that means the command couldnt be found
//sets the exit value to one and then returns
			if(getpid() != parent)
			{
				execute(inputCopy,priority);	
				printf("Couldnt find command\n");
				fflush(stdout);
				exit(1);
				return;
			}
		}
	}
}

//input nothing
//output dead children
//kills the background processes in a horrible violent manner

void shellExit()
{
	int i = 0;
	while(holder[i]!=0)
	{
		printf("%d",holder[i]);
		fflush(stdout);
		kill(holder[i],SIGKILL);
		i++;
		printf("Killed a Child");
		fflush(stdout);
	}
	exit(0);
}

//input user input
//output nothing
//purpose: to change the current directory

void shellCD(char input [2048])
{
//gets rid of cd the space and if the is an amperacand at the end it
//replaces it with a null terminator
	char path[2048];
	strcpy(path,input+3);
	if(path[strlen(path)-2] == ' ' && path[strlen(path)-1]=='&')
	{
		path[strlen(path)-2] = '\0';
	}
//finds the home path of a computer 
	char * home_path = getenv("HOME");
	int i;
//checks whether cd has no arguments and if it does changed
//directory to home directory
	if(input[0] == 'c' && input[1] == 'd' && input[2] == '\0')
	{
		i=chdir(home_path);
	}
//checks whether user input has home path and if it doesnt add it
	else if(strncmp(path,home_path,strlen(home_path))==0)
	{
		i=chdir(path);
	}
	else
	{
		char home[2048];
		strcpy(home,home_path);
		home[strlen(home_path)]= '/';
		strcpy(home + strlen(home_path)+1,path);
		i = chdir(home);
	}
//prints current working directory
	if(i == 0)
	{
		char cwd[1000];
		getcwd(cwd,sizeof(cwd));
		printf("current dir: %s\n",cwd);
		fflush(stdout);
	}
//if the directory couldnt be changed it prints this message
	else if (i == -1)
	{
		printf("Invalid File Path\n");
		fflush(stdout);
	}
}

//input user input
//output expands $$ to pid
//purpose expansion of double dollar signs to pid

int expansion(char input [2048])
{
	int i=0;
	int j=1;
	int q;
	int t= 0;
//gets rid of that pesky \n character
	if(input[strlen(input-1)] == '\n')
	{
		input[strlen(input)-1]=0;
	}
//finds the $$. would have been way easier to use strstr but i like the pain
	while(input[j]!=0)
	{
		if(input[i] == '$' && input[j] == '$')
		{
			char temp[2048];
			memset(temp,0,100);
			sprintf(temp,"%d",parent);
			char copyArray[2048];
			memset(copyArray,0,100);
			strcpy(copyArray,input);
			for(q=i; q<strlen(temp)+i; q++)
			{
				input[q]=temp[t];
				t++;
			}
			t=0;
			strcpy(input+strlen(temp)+i,copyArray+i+2);
			return 1;
		}
		i++;
		j++;
	}
//if it returns 0 it then the expansion function stop getting called
	return 0;
}

//input nothing
//output nothing
//purpose displays status of last foreground process

void shellStatus()
{
//checks whether a foreground process has been run
	if(recent_process == -10)
	{
		printf("no foreground 0\n");
		fflush(stdout);
		return;
	}
	if(errno != 10)
	{
		printf("a exit value 1\n");
		fflush(stdout);
		return;
	}
	else
	{
//checks how process was killed and then prints how it died or its exit value
		int i;
		if(i=WIFEXITED(exitMethod)!=0)
		{
			printf("exit value: %d\n",WEXITSTATUS(exitMethod));
			fflush(stdout);
		} 
		else
		{
			i = WIFSIGNALED(exitMethod);
			printf("terminated by signal %d\n",WTERMSIG(exitMethod));
			fflush(stdout);
		}
		return;
	}
}

//input a int pointer
//output nothing
//purpose prints how a child died, whether by a signal or by ending its life

void childStatus(int * childExitMethod)
{
	int i;
	if(i=WIFEXITED(childExitMethod)!=0)
	{
		printf("child exit value %d\n",i);
		fflush(stdout);
	} 
	else
	{
		i = WIFSIGNALED(childExitMethod);
		printf("child terminated by signal %d\n",WTERMSIG(childExitMethod));
		fflush(stdout);
	}



}

//input character array
//output integer
//purpose checks whether process is background or foreground

int findAnd(char input [2048])
{
	if(input[strlen(input)-1] == '&' && input[strlen(input)-2]==' ')
	{
		input[strlen(input)-2] ='\0';
		return 0;
	}
	else
	{
		return 1;
	}
}

//input character array and int
//output nothing
//purpose calls redirection and then call execvp 

void execute(char input[2048],int priority)
{
	char splitInput[512] [2048];
	int i = 0;
	int j= 0;
	int q = 0;
	char temp [2048];
	redirection(input,priority);
	while(i <  strlen(input)+1)
	{
		if(input[i] == ' ' || input[i]=='\0')
		{
			temp[j]='\0';
			j = 0;
			strcpy(splitInput[q],temp);
			q++;
		}		
		else
		{
			temp[j] = input[i];
			j++;
		}
		i++;
	}
	char * temp1[q];
	for(j = 0; j<q; j++)
	{
		temp1[j] = splitInput[j];
	}
	temp1[q] = NULL;
	if(priority ==1)
	{
//makes a new signal handler for the foreground child process
		struct sigaction sigIntHandler={0};
		sigIntHandler.sa_handler=SIG_DFL;
		sigemptyset(&sigIntHandler.sa_mask);
		sigaddset(&sigIntHandler.sa_mask,SIGINT);
		sigaction(SIGINT,&sigIntHandler,NULL);

	}
	int t = execvp(temp1[0],temp1);
}

//input character array and an int
//output nothing
//purpose performs redirection

void redirection(char input[2048],int priority)
{
	int i;
	int input_num = -1;
	int inputFlip = -1;
	int outputFlip = -1;
	int output = -1;
	char input_file [2048];
	char output_file [2048];
	char holder [2048];
	char holder2 [2048];
//checks for right and left carrets 
	for(i = 0; i < strlen(input); i++)
	{
		if(input[i] == '<')
		{
			input_num = i;
			inputFlip=1;
		}
		if(input[i] == '>')
		{
			output = i;
			outputFlip=1;
		}
	}
	int q = 0;
//lots and lots of character string manipulation. takes the left and right 
//carets and the file names out 
	if(input_num != -1)
	{
		for(i = input_num+2; i < strlen(input); i++)
		{
			if(input[i] != ' ' && input[i] != '\0' )
			{
				holder[q] = input[i];
				q++;
			}	
			else
			{
				break;
			}
		}
		holder[q]='\0';
	}
	strcpy(input_file,holder);
	q = 0;
	if(output != -1)
	{
		for(i = output+2; i < strlen(input); i++)
		{
			if(input[i] != ' ')
			{
				holder2[q] = input[i];
				q++;
			}	
			else
			{
				break;
			}
		}
		holder2[q]='\0';
	}
	strcpy(output_file,holder2);
	char endInput [2048];
	if(outputFlip == 1)
	{
		strcpy(endInput,input + output + 2);	
		if(strlen(endInput) != strlen(output_file))
		{
			strcpy(input+output,endInput+strlen(output_file)+1);
		}	
		else
		{
			input[output -1] = '\0';
		}
	}
	char endInput2 [2048];
	if(inputFlip == 1)
	{
		strcpy(endInput2,input + input_num + 2);	
		if(strlen(endInput2) != strlen(input_file))
		{
			strcpy(input+input_num,endInput2+strlen(input_file)+1);
		}	
		else
		{
			input[input_num -1] = '\0';
		}
	}
//if a process has been background this redirects input and output to dev/null
//if they havent been manually redirected by the user
	if(inputFlip == -1 && priority == 0)
	{
		int fd = open("/dev/null",O_WRONLY | O_CREAT,0020);
		dup2(fd,0);
	}
	if(outputFlip == -1 && priority == 0)
	{
		int fd = open("/dev/null",O_WRONLY | O_CREAT,0020);
		dup2(fd,1);
	}
//opens a file to write or prints an err message
	if(outputFlip == 1)
	{
		int fd = open(output_file,O_WRONLY | O_CREAT,0600);
		if(fd < 0)
		{
			printf("Couldn't Open File");
			fflush(stdout);
			return;
		}
		dup2(fd,1);
	}
//opens a read only file or prints an err message if not possible
	if(inputFlip == 1)
	{
		int fd = open(input_file,O_RDONLY,0001);
		if(fd < 0)
		{
			printf("Couldn't Open File");
			fflush(stdout);
			return;
		}
		dup2(fd,0);
	}
}

//input int
//output nothing
//purpose SIGTSTOP activates or deactivates foreground only mode

void signalStop(int signal)
{
	if(background_toggle == 0)
	{
		write(1,"Entering Foreground-only mode (& is now ignored)\n",50);
		background_toggle= 1;
	}
	else if(background_toggle == 1)
	{
		write(1,"Exiting Foreground-only mode",29);
		background_toggle= 0;
	}

	return;
}

//input: int
//output: nothing
//purpose does nothing for sigterm

void signalTerm(int signum)
{
	return;
}

//input: character string
//output: int
//purpose: checks for blank input by the user

int checkForBlank(char input[2048])
{
	int i = 0;
	for(i=0;i<strlen(input);i++)
	{
		if(input[i] != ' ' && input[i] !='\n')
		{
			return 1;
		}

	}
	return 0;
}
