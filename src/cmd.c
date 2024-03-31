// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1
#define SHELL_SUCCESS 0  // Cod pentru succes
#define SHELL_FAILED -1   // Cod pentru eroar

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	if (dir == NULL)
		return false;

	if (chdir(dir->string) == -1)
		return false;

	if (setenv("PWD", dir->string, 1) == -1)
		return false;

	return true;
}



/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */
	exit(0);

	return 0; /* TODO: Replace with actual exit code. */
}
static int external_commands(simple_command_t *s)
{
	pid_t pid = fork();

	DIE(pid == -1, "failed pid");

	if (pid == 0) {

		if (s->in != NULL) {
			char *in = get_word(s->in);

			if (s->in->next_part != NULL)
				strcat(in, get_word(s->in->next_part));

			int file_descriptor = open(in, O_RDONLY);

			DIE(file_descriptor == -1, "open stdout");
			dup2(file_descriptor, STDIN_FILENO);
			close(file_descriptor);
		}
		if (s->out != NULL && s->err != NULL) {
			char *out = get_word(s->out);
			char *err = get_word(s->err);
			int file_descript_out = open(out, O_WRONLY | O_CREAT | O_APPEND, 0644);
			int f_descript_err = open(err, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			DIE(file_descript_out == -1, "open stdout");
			DIE(f_descript_err == -1, "open stderr");

			DIE(dup2(file_descript_out, STDOUT_FILENO) == -1, "dup2 failed");
			DIE(dup2(f_descript_err, STDERR_FILENO) == -1, "dup2 failed");

			DIE(close(file_descript_out)  == -1, "close file failed");
			DIE(close(f_descript_err) == -1, "close file failed");

		} else {
			if (s->out != NULL) {
				char *out = get_word(s->out);
				int flags = O_WRONLY | O_CREAT;

				if (s->io_flags == IO_REGULAR)
					flags |= O_TRUNC;
				else if (s->io_flags == IO_OUT_APPEND)
					flags |= O_APPEND;

				int file_descriptor = open(out, flags, 0644);

				DIE(file_descriptor == -1, "open failed");
				DIE(dup2(file_descriptor, STDOUT_FILENO) == -1, "dup2 failed");
				DIE(close(file_descriptor) == -1, "close failed");
			}

			// gestionarea erorii la iesire
			if (s->err != NULL) {
				char *err = get_word(s->err);
				int flags = O_WRONLY | O_CREAT;
				int fd_err;

				if (s->io_flags == IO_REGULAR)
					fd_err = open(s->err->string, flags | O_TRUNC, 0644);
				else if (s->io_flags == IO_ERR_APPEND)
					fd_err = open(err, flags | O_APPEND, 0644);
				DIE(fd_err == -1, "open file failed");
				DIE(dup2(fd_err, STDERR_FILENO) == -1, "dup2 failed");
				DIE(close(fd_err) == -1, "close file failed");
			}
		}

		int argc;
		char **argv = get_argv(s, &argc);

		// Executarea comenzii prin incarcarea unui nou program in procesul curent
		execvp(argv[0], argv);

		// Eliberarea memoriei
		for (int i = 0; i < argc; i++)
			free(argv[i]);

		free(argv);

	} else { // Procesul părinte
		int status;

		waitpid(pid, &status, 0);
		return WEXITSTATUS(status);
	}

	return SHELL_EXIT;

}
/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */
	if (s == NULL)
		return SHELL_EXIT;
	/* TODO: If builtin command, execute the command. */

	// Dacă comanda externă nu a putut fi executată și comanda este "cd",
	// execută comanda internă "cd".
	int external_command_result;

	if (strcmp(s->verb->string, "cd") == 0) {
		int original_fds[3] = {STDOUT_FILENO, STDIN_FILENO, STDERR_FILENO};
		int duplicate_fds[3];

		// Duplicarea descriptorilor de fisiere
		for (int i = 0; i < 3; i++)
			duplicate_fds[i] = dup(original_fds[i]);

		int external_command_result = external_commands(s);

		dup2(duplicate_fds[0], STDOUT_FILENO);
		dup2(duplicate_fds[1], STDIN_FILENO);
		dup2(duplicate_fds[2], STDERR_FILENO);

		// inchiderea descriptorilor de fișiere duplicați
		for (int i = 0; i < 3; i++)
			close(duplicate_fds[i]);

		if (!shell_cd(s->params)) {
			fprintf(stderr, "cd: failed to change directory\n");
			return SHELL_FAILED;
		}
		return SHELL_SUCCESS;
		return external_command_result;
	}

	// Execută comanda "exit" dacă este cazul.
	if (strcmp(s->verb->string, "exit") == 0  || strcmp(s->verb->string, "quit") == 0)
		return shell_exit();

	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */
	if (s->verb->next_part != NULL) {
		char *word = get_word(s->verb->next_part->next_part);

		setenv(s->verb->string, word, 1);
		free(word);
		return SHELL_EXIT;
	}

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */
	 external_command_result = external_commands(s);
	return external_command_result;
}



/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	pid_t pid1 = fork();

	if (pid1 == -1)
		return false;
	if (pid1 == 0) {
		parse_command(cmd1, level, father);
		exit(0);
	}
	pid_t pid2 = fork();

	if (pid2 == -1)
		return false;
	if (pid2 == 0) {
		parse_command(cmd2, level, father);
		exit(0);
	}
	int status1;
	int status2;

	if (waitpid(pid1, &status1, 0) == -1)
		return false;

	if (waitpid(pid2, &status2, 0) == -1)
		return false;

	if (WIFEXITED(status1) && WIFEXITED(status2))
		return true;

	return false;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static int run_on_pipe(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{

	int fd[2];

	if (pipe(fd) == -1)
		return SHELL_FAILED;

	pid_t pid1 = fork();

	if (pid1 == -1)
		return SHELL_FAILED;


	if (pid1 == 0) {
		close(fd[READ]);
		dup2(fd[WRITE], STDOUT_FILENO);
		close(fd[WRITE]);
		parse_command(cmd1, level, father);
		exit(EXIT_FAILURE);
	}

	pid_t pid2 = fork();

	if (pid2 == -1) {
		close(fd[READ]);
		close(fd[WRITE]);
		return SHELL_FAILED;
	}

	if (pid2 == 0) { // Copil 2
		close(fd[WRITE]);
		dup2(fd[READ], STDIN_FILENO);
		close(fd[READ]);
		parse_command(cmd2, level, father);
		exit(EXIT_FAILURE);
	}

	close(fd[READ]);
	close(fd[WRITE]);

	int status1, status2;

	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	if (WIFEXITED(status2))
		return WEXITSTATUS(status2);

	return SHELL_FAILED;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	if (c == NULL)
		return SHELL_EXIT;
	if (c->op == OP_NONE && c->scmd == NULL)
		return SHELL_EXIT;
	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		return parse_simple(c->scmd, level, father);

		return 0; /* TODO: Replace with actual exit code of command. */
	}

	switch (c->op) {
	case OP_SEQUENTIAL:

		/* TODO: Execute the commands one after the other. */
		parse_command(c->cmd1, level + 1, c);
		parse_command(c->cmd2, level + 1, c);

		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		if (run_in_parallel(c->cmd1, c->cmd2, level + 1, c) == false)
			return SHELL_EXIT;

		break;

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one
		 * returns non zero.
		 */
		if (parse_command(c->cmd1, level + 1, c) != 0)
			return parse_command(c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one
		 * returns zero.
		 */
		if (parse_command(c->cmd1, level + 1, c) == 0)
			return parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
		if (run_on_pipe(c->cmd1, c->cmd2, level + 1, c) == SHELL_FAILED)
			return SHELL_EXIT;

		break;

	default:
		return SHELL_EXIT;
	}

	return 0; /* TODO: Replace with actual exit code of command. */
}


