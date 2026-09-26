// config.c
// ----------------------------

#include "config.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

cc_config_t local_conf = {.lastpath = "NP",
                          .password = "NP",
                          .username = "NP",
                          .port = 443,
                          .protocol = "udp"};

void config_read_config(void) {
  int res;
  char path[256];
  if (CONFIGPATH[0] == '~') {
    const char *home = getenv("HOME");
    res = snprintf(path, sizeof(path), "%s%sconfig.txt", home, CONFIGPATH + 1);
  } else {
    res = snprintf(path, sizeof(path), "%sconfig.txt", CONFIGPATH);
  }
  if (res > sizeof(path))
    printf("Warn: file path truncated!\n");
  else if (res < 0)
    printf("snprintf error");
  FILE *conf = fopen(path, "r");
  if (conf == NULL) {
    printf("Could not read config! Does %sconfig.txt exist?", CONFIGPATH);
    return;
  }

  {
    // PARSE
    char *buf = (char *)malloc(300 * sizeof(char));
    while (fgets(buf, 300, conf) != NULL) {
      buf[strcspn(buf, "\r\n")] = '\0';
      char *line = buf;
      if ((unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
          (unsigned char)buf[2] == 0xBF) {
        line += 3;
      }

      int len = strlen(line);
      while (len > 0 && isspace((int)line[len - 1])) {
        line[len - 1] = '\0';
        len--;
      }
      if (line[0] == '/' && line[1] == '/') {
        // comment
        continue;
      }
      if (strlen(line) == 0) {
        continue;
      }
      int offset = 0;
      char key[149] = {0}, value[149] = {0};
      if (sscanf(line, "%49[^ =] = %n", key, &offset) != 1)
        continue;
      strncpy(value, line + offset, sizeof(value) - 1);
#define MATCH(a) (strcmp(key, a) == 0)
#define COPYKEY(a)                                                             \
  do {                                                                         \
    strncpy(local_conf.a, value, sizeof(local_conf.a) - 1);                    \
    local_conf.a[sizeof(local_conf.a) - 1] = '\0';                             \
  } while (0)
#define COPYKEYINT(a)                                                          \
  do {                                                                         \
    local_conf.a = atoi(value);                                                \
  } while (0)
      if (MATCH("PROTOCOL"))
        COPYKEY(protocol);
      if (MATCH("LASTPATH"))
        COPYKEY(lastpath);
      if (MATCH("PORT"))
        COPYKEYINT(port);
      if (MATCH("USERNAME"))
        COPYKEY(username);
      if (MATCH("PASSWORD"))
        COPYKEY(password);
    }
    free(buf);
  }
  fclose(conf);
}

void config_write_conf(void) {
  int res;
  char filepath[256];
  char conffolder[256];
  if (CONFIGPATH[0] != '~') {
    res = snprintf(filepath, sizeof(filepath), "%sconfig.txt", CONFIGPATH);
    strncpy(conffolder, CONFIGPATH, sizeof(conffolder));
  } else {
    const char *home = getenv("HOME");
    if (home != NULL) {
      char *localpref = CONFIGPATH;
      res = snprintf(filepath, sizeof(filepath), "%s%sconfig.txt", home,
                     localpref + 1);
      res =
          snprintf(conffolder, sizeof(conffolder), "%s%s", home, localpref + 1);
    }
  }
  if (res >= (int)sizeof(filepath)) {
    printf("warn: filepath truncated!\n");
  } else if (res < 0) {
    printf("snprintf error\n");
  }

  struct stat stats;
  if (stat(conffolder, &stats) == -1) {
    if (mkdir(conffolder, 0755) == 0) {
      printf("Make folder success!\n");
      FILE *configfile = fopen(filepath, "w");
      if (configfile == NULL) {
        printf(" Could not create config file?! Error: %d\n", errno);
      } else {
	printf("Creating file...\n"); 
	fprintf(configfile, "\n");
        fclose(configfile);
      }
    } else {
      printf("Could not create %s with mode 755.\n", conffolder);
    }
  }
  FILE *configfile = fopen(filepath, "w");
  if (configfile == NULL) {
    printf("cannot open config.txt\n");
    return;
  }
  printf("Writing config!");
  fprintf(configfile, "// CrossConnect config. Auto-generated\nPROTOCOL = %s\nLASTPATH = %s\nPORT = %d\nUSERNAME = %s\nPASSWORD = %s\n\n// Set the above values to NP for strings to nullify them, or -1 for port.\n", local_conf.protocol, local_conf.lastpath, local_conf.port, local_conf.username, local_conf.password);
  fclose (configfile);
}

