#include <stdio.h>
#include <Log.h>
#include <Config.h>
#include <M2s.h>
#include <thread>

void overrideConfig(Config& config,int argc, char **argv);


Log logger(2048);
Config config;
#define MAX_PORT	20

M2s m2s;

int main(int argc, char **argv)
{

    std::thread threads[MAX_PORT];


    Sys::init();
    INFO("version : " __DATE__ " " __TIME__ "\n");
    config.setFile("m2s.json");
    config.load();
    overrideConfig(config,argc,argv);
    config.setNameSpace("serial");
    json ports =  config.getJson("ports",json::parse("[\"/dev/ttyUSB0\"]"));

    m2s.setConfig(config);
    m2s.init();
    m2s.run();

    return 0;
}


void overrideConfig(Config& config,int argc, char **argv)
{
    int  opt;

    while ((opt = getopt(argc, argv, "f:m:")) != -1) {
        switch (opt) {
        case 'm':
            config.setNameSpace("mqtt");
            config.set("host",optarg);
            break;
        case 'f':
            config.setFile(optarg);
            config.load();
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s [-f configFile] [-m mqttHost]\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}
