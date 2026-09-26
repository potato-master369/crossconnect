#include <stdint.h>
// the engine is largely based off of
// desktop-gremlin-linux.
typedef struct {
  char protocol[16];
  char lastpath[256];
  int port;
  char username[64];
  char password[64];
} cc_config_t;

extern cc_config_t local_conf;

void config_read_config(void);
void config_write_conf(void);

// path macros
// resolve ~ to $HOME
// excluding config.txt at the end
#define CONFIGPATH "~/.config/crossconnect/"
