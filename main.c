#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_LINE 80
#define PROMPT "osh>"

// Hàm cho người dùng nhập lệnh
// Input: chuỗi để chứa lệnh
// Output: trả về số ký tự của chuỗi lệnh, -1 nếu thất bại
int getCommand(char *str) 
{
	char ch;
	int i = 0;
	
	while (((ch = getchar()) != '\n') && (i < MAX_LINE)) 
	{
		str[i++] = ch;
	}

	if (i == MAX_LINE && ch != '\n')
	{
		printf("Command too long.\n");
		i = -1;
	}
	else 
	{
		str[i] = 0;
	}

	// Đọc tất cả các ký tự thừa vượt số lượng MAX_LINE trong vùng đệm
	while (ch != '\n') ch = getchar();
	return i;
}

// Hàm tách chuỗi command thành 1 danh sách các lệnh và tham số
// Input: chuỗi command, mảng chứa danh sách các lệnh và tham số
// Output: độ dài mảng chứa danh sách các lệnh và tham số
int parseCommand(char command[], char *args[])
{
	int args_num = 0;
	char* arg;

	while (arg = strtok_r(command, " ", &command))
	{
		args[args_num++] = arg;
	}

	args[args_num] = NULL;

	return args_num;
}

// Redirect đầu vào sang tập tin
// Input: tên file
int redirectInput(char dir[])
{
	int fd = open(dir, O_RDONLY);
	if (fd == -1)
	{
		printf("Can't open file.\n");
		exit(2);
	}

	if (dup2(fd, STDIN_FILENO) < 0)
	{
		printf("Failed redirecting input.\n");
		exit(2);
	}

	close(fd);

	return 0;
}

// Redirect đầu ra sang tập tin
// Input: tên file
int redirectOutput(char dir[])
{
	int fd = creat(dir, O_WRONLY | O_CREAT | S_IRWXU);
	if (fd == -1)
	{
		printf("Can't open file.\n");
		exit(2);
	}

	if (dup2(fd, STDOUT_FILENO) < 0)
	{
		printf("Failed redirecting output.\n");
		exit(2);
	}

	close(fd);

	return 0;
}

// Hàm tìm vị trí của "|" trong mảng chứa danh sách các lệnh và tham số
// Input: mảng chứa danh sách các lệnh và tham số, số lượng phần tử
// Output: trả về vị trí của "|" trong mảng, -1 nếu không tìm thấy
int findPipePosition(char *args[], int args_num)
{
	int i;
	for ( i = 0; i < args_num; i++)
	{
		if (strcmp(args[i], "|") == 0)
		{
			return i;
		}
	}
	return -1;
}

// Hàm thực thi lệnh chứa "|"
// Input: mảng chứa danh sách các lệnh và tham số, số lượng phần tử, vị trí của "|" trong mảng
int pipeProcesses(char *args[], int args_num, int pipePosition)
{
	int p[2];
	if (pipe(p) < 0)
	{
		printf("Can't create pipe.\n");
		exit(2);
	}

	// Tách các lệnh và tham số sau "|" thành mảng riêng (args2[])
	char *args2[MAX_LINE];
	int i;
	for ( i = pipePosition + 1; i < args_num; i++)
	{
		args2[i - pipePosition - 1] = args[i];
	}

	int args2_num = args_num - pipePosition - 1;
	args_num = pipePosition;

	args[args_num] = NULL;
	args2[args2_num] = NULL;

	pid_t pidProcess2 = fork();

	if (pidProcess2 < 0)	// lỗi fork()
	{
		printf("Can't fork process.\n");
		exit(2);
	}
	else if (pidProcess2 == 0)	// process con
	{
		// Redirect đầu vào sang pipe (process sẽ đợi process khác viết dữ liệu vào pipe rồi mới đọc và thực thi)
		if (dup2(p[0], STDIN_FILENO) < 0)
		{
			printf("Failed redirecting input.\n");
			exit(2);
		}
		close(p[0]);
		close(p[1]);

		// Thực thi lệnh sau "|"
		if (execvp(args2[0], args2) == -1)
		{
			printf("Failed to execute the command.\n");
			exit(2);
		}
	}
	else	// process cha
	{
		// Process cha tiếp tục tạo process để thực thi lệnh
		pidProcess2 = fork();

		if (pidProcess2 < 0)	// lỗi fork()
		{
			printf("Can't fork process.\n");
			exit(2);
		}
		else if (pidProcess2 == 0)	// process con
		{
			// Redirect đầu ra sang pipe
			if (dup2(p[1], STDOUT_FILENO) < 0)
			{
				printf("Failed redirecting output.\n");
				exit(2);
			}
			close(p[1]);
			close(p[0]);

			// Thực thi lệnh trước "|"
			if (execvp(args[0], args) == -1)
			{
				printf("Failed to execute the command.\n");
				exit(2);
			}
		}
		else
		{
			close(p[0]);
			close(p[1]);
			waitpid(pidProcess2, NULL, 0);
		}
		// Đợi các process con hoàn tất
		wait(NULL);
	}
}

// Hàm thực thi lệnh
// Input: mảng chứa danh sách các lệnh và tham số, số phần tử mảng
int executeCommand(char* args[], int args_num)
{
	// Kiểm tra nếu có pipe liên kết 2 lệnh
	int pipePosition;

    // Kiểm tra nếu có redirect input sang file
	if (args_num > 2 && strcmp(args[args_num - 2], "<") == 0)
	{
		redirectInput(args[args_num - 1]);
		args[args_num - 2] = NULL;
		args_num = args_num - 2;
	}

	// Kiểm tra nếu có redirect output sang file
	if (args_num > 2 && strcmp(args[args_num - 2], ">") == 0)
	{
		redirectOutput(args[args_num - 1]);
		args[args_num - 2] = NULL;
		args_num = args_num - 2;
	}

	if (args_num > 2 && (pipePosition = findPipePosition(args, args_num)) != -1)
	{
		pipeProcesses(args, args_num, pipePosition);
		exit(EXIT_SUCCESS);
	}

	if (execvp(args[0], args) == -1)
	{
		printf("Failed to execute the command.\n");
		exit(2);
	}

	return 0;
}

int main()
{
    bool running = true;
	int flagWait;
	char command[MAX_LINE + 1];
	char *args[MAX_LINE / 2 + 1];
	int args_num;
	char preCommand[MAX_LINE + 1] = "";
	pid_t pid;

	while (running)
	{
		flagWait = 1;
		printf(PROMPT);
		fflush(stdout);

		int cmdLenght = getCommand(command);
		
		// Tách chuỗi lệnh thành mảng các lệnh và tham số 
		args_num = parseCommand(command, args);
		
		// Không thể nhập lệnh hoặc lệnh rỗng
		if (cmdLenght <= 0) continue;

		// Kiểm tra lệnh trước đó
		if (strcmp(args[0], "!!") == 0)
		{
			if (strcmp(preCommand, "") == 0)
			{
				printf("No command in history.\n");
			}
			strcpy(command, preCommand);
		}
		
		// Lưu chuỗi lệnh đang thực thi vào lịch sử
		strcpy(preCommand, command);
		// Kiểm tra nếu "exit"
		if (strcmp(args[0], "exit") == 0)
		{
			running = false;
			continue;
		}

		// Kiểm tra nếu "cd"
		if (strcmp(args[0], "cd") == 0)
		{
			if (chdir(args[1]) < 0)
			{
				printf("Can't change directory.\n");
			}
			continue;
		}

		// Kiểm tra nếu có chạy ngầm
		if (strcmp(args[args_num - 1], "&") == 0)
		{
			flagWait = 0;
			args[args_num - 1] = NULL;
			args_num--;
		}

		pid = fork();

		if (pid == 0)	// Process con
		{
			executeCommand(args, args_num);
		}
		else if (pid > 0)	// Process cha
		{
			if (flagWait == 0)
			{
				continue;
			}
			waitpid(pid, NULL, 0);
		}
		else	// Lỗi fork
		{
			printf("Can't fork process.\n");
		}
		
	}
	return 0;
}