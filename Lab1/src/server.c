#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// will be imported from httpd.conf file in main()
char root_directory[50];
char index_filename[50];

void write_to_log_file(int log_type, char *content, char *request_type)
{
  // gets date and time representation
  char s[64];
  time_t now   = time(NULL);
  struct tm *p = localtime(&now);
  strftime(s, 64, "%c", p);

  FILE *log_file;
  if (log_type > -1)
    log_file = fopen("../logs/access.log", "a");
  else
    log_file = fopen("../logs/error.log", "a");

  if (log_file == NULL)
    printf("Error opening log file");
  else
    fprintf(log_file, "[%s] %s: %s\n", s, request_type, content);
  fclose(log_file);

  if (log_type == -2)
    printf("%s\n", content);
  return;
}

int send_to_cgi(int sock_file_descriptor, char *request_type, char *path)
{
  close(1);
  dup2(sock_file_descriptor, 1);

  char *arr[] = {path, NULL};

  setenv("QUERY_STRING", path, 1);
  int result;

  if (strcmp(request_type, "POST") == 0)
    result = execv("../cgi-bin/POST.cgi", arr); // pass your script_name
  else
    result = execv("../cgi-bin/GET.cgi", arr); // pass your script_name

  if (result < 0) {
    write_to_log_file(-2, "", "ERROR 500 Internal Server Error");
    char *send_500_error = "HTTP/1.0 500 Internal Server Error\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 25\r\n"
                           "Connection: close\r\n"
                           "Last-Modified: Mon, 23 Mar 2020 02:49:28 GMT\r\n"
                           "Expires: Sun, 17 Jan 2038 19:14:07 GMT\r\n"
                           "Date: Mon, 23 Mar 2020 04:49:28 GMT\n\n" 
                           "500 Internal Server Error";
    send(sock_file_descriptor, send_500_error, strlen(send_500_error), 0);
  }
  return 0;
}

int sock_from_client(int sock_file_descriptor)
{
  char *data_to_client;
  char *method_type, *filename;
  char buffer[2048] = {0};
  char buffer2[2048];

  recv(sock_file_descriptor, buffer, 2048, 0);
  memcpy(buffer2, buffer, sizeof(buffer));

  char *get_first_line      = strtok(buffer, "\n");
  char *get_words_from_line = strtok(get_first_line, " ");

  // get method type & file name from first line of buffer
  // ex) "GET" type and "/index.htm" filename in GET /index.htm HTTP/1.0
  for (int i = 0; i < 2; i++) {
    if (i == 0) {
      method_type = get_words_from_line;
    }
    else { // redirect to index_filename if no filename is given
      filename = (strcmp(get_words_from_line, "/") == 0) ? index_filename
                                                         : get_words_from_line;
    }
    get_words_from_line = strtok(NULL, " "); // sets pointer to next "word"
  }

  // If the client wants to look for an index.html file, then the server must
  // redirect to the right path (outside of /src/ and /contents/ directories).
  // Otherwise, look for a specific file in /contents/ (AKA root_directory).
  if ((strcmp(filename, "/index.html") == 0) ||
      (strcmp(filename, "../index.html") == 0)) {
    memset(root_directory, '\0', sizeof(root_directory));
    strcpy(root_directory, "../index.html");
  }
  else {
    if (!(strstr(filename, ".cgi"))) {
      strcat(root_directory, filename);
    }
  }

  if ((access(root_directory, F_OK) < 0) && (!(strstr(filename, ".cgi")))) {
    write_to_log_file(-1, filename, "ERROR (file not found)");

    data_to_client = "HTTP/1.0 404 Not Found\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 25\r\n"
                     "Connection: close\r\n"
                     "Last-Modified: Mon, 23 Mar 2020 02:49:28 GMT\r\n"
                     "Expires: Sun, 17 Jan 2038 19:14:07 GMT\r\n"
                     "Date: Mon, 23 Mar 2020 04:49:28 GMT\n\n" 
                     "HTTP 404 - File not found";
    send(sock_file_descriptor, data_to_client, strlen(data_to_client), 0);
  }
  else {
    if (strcmp(method_type, "GET") == 0) {
      write_to_log_file(1, filename, "GET request");
      printf("Received GET method (%s)\n", filename);

      send_to_cgi(sock_file_descriptor, "GET", root_directory);
    }
    else if (strcmp(method_type, "POST") == 0) {
      write_to_log_file(1, filename, "POST request");
      printf("Received POST method (%s)\n", filename);

      // get last line of buffer to get the user's inputs.
      char *last_line;
      char *line_selector = strtok(buffer2, "\n");

      while (line_selector != NULL) {
        last_line     = line_selector;
        line_selector = strtok(NULL, " ");
      }
      last_line = strtok(last_line, "\n");
      last_line = strtok(NULL, "\r\n");

      send_to_cgi(sock_file_descriptor, "POST", last_line);
    }
    else {
      write_to_log_file(-2, filename, "ERROR 501: not implemented request");
      char *send_501_error = "HTTP/1.0 501 Not Implemented\r\n"
                             "Content-Type: text/plain\r\n"
                             "Content-Length: 19\r\n"
                             "Connection: close\r\n"
                             "Last-Modified: Mon, 23 Mar 2020 02:49:28 GMT\r\n"
                             "Expires: Sun, 17 Jan 2038 19:14:07 GMT\r\n"
                             "Date: Mon, 23 Mar 2020 04:49:28 GMT\n\n" 
                             "501 Not Implemented";
      send(sock_file_descriptor, send_501_error, strlen(send_501_error), 0);
    }
  }

  return 0;
}

int main(int argc, char const *argv[])
{
  // will be imported from httpd.conf file
  int simultaneous_connections, port;

  printf("\nImporting settings from /conf/httpd.conf...\n");
  FILE *ptr = fopen("../conf/httpd.conf", "r");
  if (ptr == NULL) {
    write_to_log_file(-2, "Opening ../conf/httpd.conf file failed.", "ERROR");
    exit(EXIT_FAILURE);
  }

  // read information from httpd.conf line by line
  if ((fscanf(ptr, "%i", &simultaneous_connections) < 0) ||
      (fscanf(ptr, "%s", root_directory) < 0) ||
      (fscanf(ptr, "%s", index_filename) < 0) ||
      (fscanf(ptr, "%i", &port) < 0)) {
    printf(" Configuration failed - make sure you include:\n"
           "   - number of simultaneous connections\n"
           "   - root directory to start looking for HTML files\n"
           "   - index filename, if none given\n"
           "   - port number to run on the server\n");
    fclose(ptr);
    write_to_log_file(-1, "Settings from /conf/httpd.conf failed", "ERROR");
    exit(EXIT_FAILURE);
  }
  fclose(ptr); // closing the file

  printf("  Number of simultaneous connections: %i\n",
         simultaneous_connections);
  printf("  Root directory (to start looking for HTML files): %s\n",
         root_directory);
  printf("  Index filename (if none given): %s\n", index_filename);
  printf("  Port to run on server: %i\n\n", port);

  int s, new_sock;            // socket descriptor
  struct sockaddr_in address; // an Internet endpoint address
  socklen_t sock_size = sizeof(struct sockaddr_in);

  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port        = htons(port); // byte order

  // Allocate a socket
  s = socket(AF_INET, SOCK_STREAM, 0); // TCP uses SOCK_STREAM
  if (s < 0) {
    write_to_log_file(-2, "Socket failed - can't be created.", "ERROR");
    exit(EXIT_FAILURE);
  }

  // Bind the socket
  if (bind(s, (struct sockaddr *)&address, sizeof(address)) < 0) {
    write_to_log_file(-2, "Socket binding failed.", "ERROR");
    exit(EXIT_FAILURE);
  }

  // Put server to passive mode
  if (listen(s, simultaneous_connections) < 0) {
    write_to_log_file(-2, "listen() failed.", "ERROR");
    exit(EXIT_FAILURE);
  }

  // Accepting new connections
  while (1) {
    new_sock = accept(s, (struct sockaddr *)&address, &sock_size);

    if (new_sock < 0) {
      write_to_log_file(-2, "New socket - accept() failed.", "ERROR");
      exit(EXIT_FAILURE);
    }
    else {
      // "Fork() can be used at the place where there is division of work like
      // a server has to handle multiple clients, So parent has to accept the
      // connection on regular basis, So server does fork for each client"
      int pid = fork();

      if (pid == 0) { // child
        close(s);     // I am now the client - close the listener: client doesnt
                      // need it
        sock_from_client(new_sock);
        exit(0);
      }
      else if (pid > 0) { // parent
        close(new_sock);  // I am still the server - close the accepted handle:
                          // server doesn't need it.
      }
      else {
        write_to_log_file(-2, "fork() failed.", "ERROR");
        exit(EXIT_FAILURE);
      }
    }
  }

  return 0;
}