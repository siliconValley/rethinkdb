
#ifndef __CMD_ARGS_HPP__
#define __CMD_ARGS_HPP__

#include "config/args.hpp"

struct cmd_config_t {
    int port;
    int max_cores;
    char db_file_name[MAX_DB_FILE_NAME];
    size_t max_cache_size;
    bool delay_commits;
    unsigned int flush_interval_ms;
};

void parse_cmd_args(int argc, char *argv[], cmd_config_t *config);

#endif // __CMD_ARGS_HPP__

