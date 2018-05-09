#include "M2s.h"

using std::string;
using std::vector;

typedef struct {
    uint32_t baudrate;
    uint32_t symbol;
} BAUDRATE;

BAUDRATE BAUDRATE_TABLE[] = { { 50, B50 }, { 75, B75 }, { 110, B110 }, { 134, B134 }, //
    { 150, B150 }, { 200, B200 }, { 300, B300 }, { 600, B600 }, {	1200,	B1200	},
    { 1800, B1800 }, { 2400, B2400 }, { 4800, B4800 }, { 9600, B9600 }, //
    { 19200, B19200 }, { 38400, B38400 }, { 57600, B57600 }, //
    { 115200, B115200 }, { 230400, B230400 }, { 460800, B460800 }, //
    { 500000, B500000 }, { 576000, B576000 }, { 921600, B921600 }, //
    { 1000000, B1000000 }, { 1152000, B1152000 }, { 1500000, B1500000 }, //
    { 2000000, B2000000 }, { 2500000, B2500000 }, { 3000000, B3000000 }, //
    { 3500000, B3500000 }, { 4000000, B4000000 }
};

M2s::M2s()
{
}

M2s::~M2s()
{
}

void M2s::setConfig(Config config)
{
    _config=config;
}

int baudSymbol(uint32_t br)
{
    for (uint32_t i = 0; i < sizeof(BAUDRATE_TABLE) / sizeof(BAUDRATE); i++)
        if (BAUDRATE_TABLE[i].baudrate == br)
            return BAUDRATE_TABLE[i].symbol;
    INFO("connect: baudrate %d  not found, default to 115200.", br);
    return B115200;
}

void M2s::stdinSetup()
{
    struct termios ctrl;
    tcgetattr(STDIN_FILENO, &ctrl);
    ctrl.c_lflag &= ~( ICANON | ECHO ); // turning off canonical mode makes input unbuffered
    tcsetattr(STDIN_FILENO, TCSANOW, &ctrl);
}


void M2s::init()
{
    _config.setNameSpace("mqtt");
    _config.get("port",_mqttPort,1883);
    _config.get("host",_mqttHost,"test.mosquitto.org");
    std::string defaultDevice;
    defaultDevice.append(Sys::hostname()).append(".USB0");
    _config.get("serialDevice",_mqttSerialDevice,defaultDevice);
    _mqttSerialTopic="src/"+_mqttSerialDevice+"/serial2mqtt/";
    _mqttSubscribedTo = _mqttSerialTopic+"#";
    _mqttSerialLogTopic = _mqttSerialTopic+"log";
    _mqttClientId = Sys::hostname();
    _mqttClientId += "-";
    _mqttClientId += std::to_string(Sys::millis());
    _mqttLogicalDeviceNameTopic = _mqttSerialTopic+"device";
    INFO(" waiting for logical device name on %s ",_mqttLogicalDeviceNameTopic.c_str());
    _config.get("keepAliveInterval",_mqttKeepAliveInterval,20);
    _config.get("willTopic",_mqttWillTopic,_mqttSerialTopic+"/system/alive");
    _config.get("willMessage",_mqttWillMessage,"false");
    _mqttWillQos=0;
    _mqttWillRetained=false;
    _mqttLogicalDeviceTopic="";
    _config.setNameSpace("programmer");
    _config.get("binFile",_binFileName,"image.bin");
    INFO(" loading bin file : %s ",_binFileName.c_str());

    if (pipe(_signalFd) < 0)        INFO("Failed to create pipe: %s (%d)", strerror(errno), errno);

    if (fcntl(_signalFd[0], F_SETFL, O_NONBLOCK) < 0)
        INFO("Failed to set pipe non-blocking mode: %s (%d)", strerror(errno), errno);
}

void M2s::threadFunction(void* pv)
{
    run();
}

void M2s::run()
{
    std::string line;
    Timer mqttTimer;
    Timer binFileTimer;

    mqttTimer.atInterval(1000).doThis([this]() {
        if ( !_mqttConnected) {
            _mqttConnecting=true;
            mqttConnect();
        } else {
            mqttPublish("src/pc/m2s/alive","true",0,0);
        }
    });
    binFileTimer.atDelta(5000).doThis([this]() {
        if ( false ) { // check bin file changed
            // sending new bin file
        }

    });
    mqttConnect();
    stdinSetup();

    while(true) {
        while(true) {
            Signal s = waitSignal(1000);
            mqttTimer.check();
            binFileTimer.check();
            switch(s) {
            case TIMEOUT: {
                break;
            }
            case KEY_PRESSED : {
                char buffer;
                read(_stdinFd,&buffer,1);
                if ( buffer == 'm' ) signal(KEY_TOGGLE_SHOW_MQTT);
                if ( buffer == 'p') signal(KEY_PROGRAM);
                break;
            }
            case KEY_TOGGLE_SHOW_MQTT : {
                INFO(" TOGGLE MQTT view ");
                _showMqtt = !_showMqtt;
                break;
            }
            case KEY_PROGRAM : {
                INFO(" Programming... ");
                sendBinFile("dst/"+_mqttSerialDevice+"/serial2mqtt/flash",_binFileName);
                // read bin file and send as blob
                break;
            }
            case SERIAL_ERROR : {
                break;
            }
            case MQTT_CONNECT_SUCCESS : {
                INFO("MQTT_CONNECT_SUCCESS ");
                _mqttConnected=true;
                mqttSubscribe(_mqttSubscribedTo);
                break;
            }
            case MQTT_CONNECT_FAIL : {
                INFO("MQTT_CONNECT_FAIL ");
                _mqttConnected=false;
                break;
            }
            case MQTT_DISCONNECTED: {
                INFO("MQTT_DISCONNECTED ");
                _mqttConnected=false;
                break;
            }
            case MQTT_SUBSCRIBE_SUCCESS: {
                INFO("MQTT_SUBSCRIBE_SUCCESS");
                break;
            }
            case MQTT_SUBSCRIBE_FAIL: {
                WARN("MQTT_SUBSCRIBE_FAIL");
                mqttDisconnect();
                break;
            }
            case MQTT_ERROR : {
                WARN("MQTT_ERROR %s ",_mqttHost);
                break;
            }
            case PIPE_ERROR : {
                WARN("PIPE_ERROR %s ",strerror(errno));
                break;
            }
            case MQTT_PUBLISH_SUCCESS: {
                break;
            }
            case MQTT_MESSAGE_RECEIVED: {
                break;
            }
            default: {
                WARN("received signal '%c' for %s ",s,_mqttSerialTopic);
            }
            }
        }
    }
}


void M2s::signal(uint8_t m)
{
    if ( write(_signalFd[1],(void*)&m,1) < 1 ) {
        INFO("Failed to write pipe: %s (%d)", strerror(errno), errno);
    }
//	INFO(" signal '%c' ",m);
}


M2s::Signal M2s::waitSignal(uint32_t timeout)
{
    Signal returnSignal=TIMEOUT;
    Bytes bytes(1024);
    uint8_t buffer[1024];
    fd_set rfds;
    fd_set wfds;
    fd_set efds;
    struct timeval tv;
    int retval;
    uint64_t start = Sys::millis();
    //    uint64_t delta=1000;
    {

        // Wait up to 1000 msec.
        uint64_t delta = timeout;

        tv.tv_sec = delta / 1000;
        tv.tv_usec = (delta * 1000) % 1000000;

        // Watch serialFd and tcpFd  to see when it has input.
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        if(_stdinFd>=0) {
            FD_SET(_stdinFd, &rfds);
            FD_SET(_stdinFd, &efds);
        }
        if(_signalFd[0]) {
            FD_SET(_signalFd[0], &rfds);
            FD_SET(_signalFd[0], &efds);
        }
        int maxFd = _stdinFd < _signalFd[0] ? _signalFd[0] : _stdinFd;
        maxFd += 1;

        start = Sys::millis();

        retval = select(maxFd, &rfds, NULL, &efds, &tv);

        uint64_t waitTime = Sys::millis() - start;
        if(waitTime == 0) {
            TRACE(" waited %ld/%ld msec.", waitTime, delta);
        }

        if(retval < 0) {
            WARN(" select() : error : %s (%d)", strerror(errno), errno);
        } else if(retval > 0) { // one of the fd was set
            if(FD_ISSET(_stdinFd, &rfds)) {
                return KEY_PRESSED;
            }
            if(FD_ISSET(_signalFd[0], &rfds)) {
                ::read(_signalFd[0], buffer,1); // read 1 event
                return (Signal)buffer[0];
            }
            if(FD_ISSET(_stdinFd, &efds)) {
                WARN("serial  error : %s (%d)", strerror(errno), errno);
                return SERIAL_ERROR;
            }
            if(FD_ISSET(_signalFd[0], &efds)) {
                WARN("pipe  error : %s (%d)", strerror(errno), errno);
                return PIPE_ERROR;
            }
        } else {
            TRACE(" timeout %llu", Sys::millis());
            returnSignal=TIMEOUT;
            // TODO publish TIMER_TICK
            //           eb.publish(H("sys"),H("tick"));
        }
    }
    return (Signal)returnSignal;
}




std::vector<std::string> split(const std::string &text, char sep)
{
    std::vector<std::string> tokens;
    std::size_t start = 0, end = 0;
    while ((end = text.find(sep, start)) != std::string::npos) {
        tokens.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    tokens.push_back(text.substr(start));
    return tokens;
}

bool startsWith(std::string src,std::string pattern)
{
    return strncmp(src.c_str(),pattern.c_str(),pattern.size())==0;
}


void M2s::serialPublish(std::string topic,Bytes message,int qos,bool retained)
{
    std::string msg;
    msg.append((char*)message.data(),message.length());
    if ( startsWith(topic,_mqttSerialLogTopic ) ) {
        printf("%s\n",msg.c_str());
    } else {
        if ( topic.compare(_mqttLogicalDeviceNameTopic)==0) {
            if ( msg.compare(_mqttLogicalDeviceTopic) != 0 ) {
                INFO(" subscribed to device : %s",msg.c_str());
                mqttSubscribe("src/"+msg+"/#");
                _mqttLogicalDeviceTopic = msg;
            }
        }
        if ( _showMqtt )
            printf(" %s : %s \n",topic.c_str(),msg.c_str());
    }
}

/*
 *
						@     @  @@@@@  @@@@@@@ @@@@@@@
						@@   @@ @     @    @       @
						@ @ @ @ @     @    @       @
						@  @  @ @     @    @       @
						@     @ @   @ @    @       @
						@     @ @    @     @       @
						@     @  @@@@ @    @       @

 *
 */

Erc M2s::mqttConnect()
{
    std::string connection;
    int rc;

    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    MQTTAsync_willOptions will_opts = MQTTAsync_willOptions_initializer;

    connection = "tcp://"+ _mqttHost+":";
    connection +=std::to_string(_mqttPort);
    INFO(" MQTT connecting %s as %s for %s ",connection.c_str(),_mqttClientId.c_str(),_mqttSerialTopic.c_str());
    rc = MQTTAsync_create(&_client, connection.c_str(), _mqttClientId.c_str(),
                          MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if ( rc )  WARN(" MQTTAsync_create() error ");

    rc=MQTTAsync_setCallbacks(_client, this, onConnectionLost, onMessage, onDeliveryComplete); //TODO add ondelivery
    if ( rc )  WARN(" MQTTAsync_setCallbacks() error ");

    conn_opts.keepAliveInterval = _mqttKeepAliveInterval;
    conn_opts.cleansession = 1;
    conn_opts.onSuccess = onConnectSuccess;
    conn_opts.onFailure = onConnectFailure;
    conn_opts.context = this;
    conn_opts.password =NULL;
    conn_opts.username =NULL;
//   conn_opts.retryInterval=4;
//    conn_opts.automaticReconnect=true;

    will_opts.message = _mqttWillMessage.c_str();
    will_opts.topicName = _mqttWillTopic.c_str();
    will_opts.qos = _mqttWillQos;
    will_opts.retained = _mqttWillRetained;
    conn_opts.will = &will_opts;
    conn_opts.will = 0;
    if ((rc = MQTTAsync_connect(_client, &conn_opts)) != MQTTASYNC_SUCCESS) {
        WARN("MQTTAsync_connect() failed, return code %d", rc);
        return E_NOT_FOUND;
    }
    _mqttConnecting=true;
    return E_OK;
}

void M2s::mqttDisconnect()
{
    MQTTAsync_disconnectOptions disc_opts =
        MQTTAsync_disconnectOptions_initializer;
    disc_opts.onSuccess = onDisconnect;
    disc_opts.context=this;
    int rc = 0;
    if ((rc = MQTTAsync_disconnect(_client, &disc_opts))
        != MQTTASYNC_SUCCESS) {
        WARN("Failed to disconnect, return code %d", rc);
        return;
    }
    MQTTAsync_destroy(&_client);
    _mqttConnecting=false;
    _mqttConnected=false;
}

void M2s::mqttSubscribe(std::string topic)
{
    int qos=0;
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    INFO("Subscribing to topic %s for client %s using QoS%d",
         topic.c_str(), _mqttClientId.c_str(), qos);
    opts.onSuccess = onSubscribeSuccess;
    opts.onFailure = onSubscribeFailure;
    opts.context = this;
    int rc = E_OK;

    if ((rc = MQTTAsync_subscribe(_client, topic.c_str(), qos, &opts))
        != MQTTASYNC_SUCCESS) {
        ERROR("Failed to start subscribe, return code %d", rc);
        signal(MQTT_SUBSCRIBE_FAIL);
    } else {
        INFO(" subscribe send ");
    }
}

void M2s::onConnectionLost(void *context, char *cause)
{
    M2s* me = (M2s*)context;
//   me->mqttDisconnect();
    me->signal(MQTT_DISCONNECTED);
}
int M2s::onMessage(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    M2s* me = (M2s*)context;
    Bytes msg((uint8_t*) message->payload, message->payloadlen);
    std::string topic(topicName,topicLen);

    me->serialPublish(topic,msg,message->qos,message->retained);

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    me->signal(MQTT_MESSAGE_RECEIVED);
    return 1;
}

void M2s::onDeliveryComplete(void* context, MQTTAsync_token response)
{
//    M2s* me = (M2s*)context;
}

void M2s::onDisconnect(void* context, MQTTAsync_successData* response)
{
    M2s* me = (M2s*)context;
    me->signal(MQTT_DISCONNECTED);
}

void M2s::onConnectFailure(void* context, MQTTAsync_failureData* response)
{
    M2s* me = (M2s*)context;
    WARN("Connection failure rc %d ",response ? response->code : 0);
    me->signal(MQTT_CONNECT_FAIL);
}

void M2s::onConnectSuccess(void* context, MQTTAsync_successData* response)
{
    M2s* me = (M2s*)context;
    me->signal(MQTT_CONNECT_SUCCESS);
}

void M2s::onSubscribeSuccess(void* context, MQTTAsync_successData* response)
{
    M2s* me = (M2s*)context;
    me->signal(MQTT_SUBSCRIBE_SUCCESS);
}

void M2s::onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
    M2s* me = (M2s*)context;
    me->signal(MQTT_SUBSCRIBE_FAIL);
}

void M2s::mqttPublish(std::string topic,std::string message,int qos,bool retained)
{
    Str msg(message.length()+2);
    msg=message.c_str();
    mqttPublish(topic,msg,qos,retained);
}

void M2s::mqttPublish(std::string topic,Bytes message,int qos,bool retained)
{
    if ( !_mqttConnected) {
        INFO("mqttPublish waiting connect ");
        return;
    }
    qos=1;
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    MQTTAsync_message pubmsg = MQTTAsync_message_initializer;

//    INFO("mqttPublish %s",topic.c_str());

    int rc = E_OK;
    opts.onSuccess = onPublishSuccess;
    opts.onFailure = onPublishFailure;
    opts.context = this;

    pubmsg.payload = message.data();
    pubmsg.payloadlen = message.length();
    pubmsg.qos = qos;
    pubmsg.retained = retained;

    if ((rc = MQTTAsync_sendMessage(_client, topic.c_str(), &pubmsg, &opts))
        != MQTTASYNC_SUCCESS) {
        signal(MQTT_DISCONNECTED);
        ERROR("MQTTAsync_sendMessage failed.");
    }
}
void M2s::onPublishSuccess(void* context, MQTTAsync_successData* response)
{
    M2s* me = (M2s*)context;
    me->signal(MQTT_PUBLISH_SUCCESS);
}
void M2s::onPublishFailure(void* context, MQTTAsync_failureData* response)
{
    M2s* me = (M2s*)context;
    me->signal(MQTT_PUBLISH_FAIL);
}


/*

######  ######  #######  #####  ######     #    #     # #     # ####### ######
#     # #     # #     # #     # #     #   # #   ##   ## ##   ## #       #     #
#     # #     # #     # #       #     #  #   #  # # # # # # # # #       #     #
######  ######  #     # #  #### ######  #     # #  #  # #  #  # #####   ######
#       #   #   #     # #     # #   #   ####### #     # #     # #       #   #
#       #    #  #     # #     # #    #  #     # #     # #     # #       #    #
#       #     # #######  #####  #     # #     # #     # #     # ####### #     #


*/

void M2s::sendBinFile(std::string topic,std::string binFileName)
{
    FILE *f = NULL;
    long len = 0;
    uint8_t *data = NULL;

    /* open in read binary mode */
    f = fopen(binFileName.c_str(),"rb");
    if ( f!=NULL) {
        /* get the length */
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);

        data = (uint8_t*)malloc(len + 1);

        fread(data, 1, len, f);
        Bytes bytes(0);
        bytes.map(data,len);
        mqttPublish(topic,bytes,0,false);
        INFO(" send binary to %s ",topic.c_str());
        free(data);
        fclose(f);
    } else {
        WARN(" binary file %s not found.",binFileName.c_str());
    }
}
