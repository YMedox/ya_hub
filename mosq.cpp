#include <mosquitto.h>
#include "ya_hub.hpp"


//Сохраняет статусы устройств, на которые действует постоянная подписка. Только на эти.
//И отправляет их в Яндекс. Для таких устройств не нужно выставлять таймеры обновления
void saveMQTTDeviceState(std::string device_id, std::string state) {
  logcppdebug<<"Saving to database device "<< device_id<<": "<<state<<ENDL;
    std::string save_err = "Error saving MQTT state for device '" + device_id + "': ";
    try {
      Session mySession(*mysqlSettings);
      Schema myDB = mySession.getDefaultSchema();
      Table cap  = myDB.getTable("mqttstat");
      RowResult res = cap.select("state")
                         .where("id_for_yandex=:i_d")
                         .bind("i_d", device_id).execute();
      std::string now1 = getDateStringForDB();
      if(res.count()) { //будем делать апдейт
         cap.update()
            .set("state", state)
            .set("updated", now1)
            .where("id_for_yandex=:i_d")
            .bind("i_d", device_id).execute();
      } else {          //будем сохранять новую запись
         cap.insert("id_for_yandex", "state", "updated")
            .values(device_id, state, now1).execute();
      }
      reportDeviceStatus(device_id); //каждый раз при обновлении статуса будем репортить в Yandex
    } catch(const mysqlx::Error &err) {
      logcpperror<<save_err<<err.what()<<ENDL;
    } catch(const std::exception &err) {
      logcpperror<<save_err<<err.what()<<ENDL;
    }
}

// получает сохраненный ранее статус. Если мы не ожидаем ответа, то берем старый. Актуально для устройств на батарейках, например
void getMQTTDeviceState(std::string& device_id) {
   std::string ret = getSQLStringValue("select state from mqttstat where id_for_yandex='"+device_id+"'");
   logcppdebug<<"Device "<<device_id<<" saved state is: "<<ret<<ENDL;
   waitForResponse.setDeviceResponse(ret);  //можно не запрещать, так как нет ожидания, мы берем из базы
}

// вызывается при получении сообщения MQTT
void server_message_callback(struct mosquitto *mosq, void *data, const struct mosquitto_message *message) {
	if(message->payloadlen){
		logmessage(INFO, "Got message from mosquitto %s=%s", message->topic, message->payload);
        std::string* ptr = static_cast<std::string*>(data);
		if(!ptr->empty()) {  //если подписка постоянная.
		  saveMQTTDeviceState(*ptr, std::string((char*)(message->payload)));
		} else {             //а это подписка только для получения статуса.
          waitForResponse.setDeviceResponse(std::string((char*)(message->payload)));
          waitForResponse.allow();
        }
	} else {
		logmessage(WARN, "%s (null)", message->topic);
	}
}

// можно обойтись и без этой функции
void server_publish_callback(struct mosquitto *mosq, void *data, uint16_t mid) {
    logmessage(INFO, "Published mid: %d", mid);
}

// вызывается при успешном соединении с MQTT-сервером
void server_connect_callback(struct mosquitto *mosq, void *data, int result) {
	if(!result){
		/* Subscribe to broker information topics on successful connect. */
	} else {
		logcpperror<<"Connect to mosquitto failed: "<<mosquitto_strerror(result)<<ENDL;
	}
}

// вызывается при разрыве соединения с MQTT-сервером
void server_disconnect_callback(struct mosquitto *mosq, void *data, int result) {
    logcppwarn<<"Mosquitto disconnected: "<<mosquitto_strerror(result)<<ENDL;
}

// вызывается при успешном выполнении подписки. Используетс для защелкивания ожидания ответа.
void server_subscribe_callback(struct mosquitto *mosq, void *data, int mid, int qos_count, const int *granted_qos) {
	int i, v = int(granted_qos[0]);
    logcppinfo<<"Subscribed (mid: "<<mid<<"): "<<v;
	for(i=1; i<qos_count; i++){
	    v = int(granted_qos[i]);
        (*logger)<<", "<<v;
	}
    (*logger)<<ENDL;
    waitForResponse.allow();
}

// вызывается для сообщений лога mosquitto
void my_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str) {
  switch(level) {
    case MOSQ_LOG_INFO:
    case MOSQ_LOG_NOTICE:
      *logger<<INFO;
      break;
    case MOSQ_LOG_WARNING:
      *logger<<WARN;
      break;
    case MOSQ_LOG_ERR:
      *logger<<ERROR;
      break;
    case MOSQ_LOG_DEBUG:
      *logger<<DEBUG;
      break;
  }
  *logger<<"MQTT: "<<str<<ENDL;
}

// Класс для удобства работы с подписками устройств.
// У каждого экземпляра свой loop, работают независимо друг друга, могут коннектиться к разным MQTT-серверам
cMosquittoClient::cMosquittoClient(std::string host, std::string response_topic, uint32_t timeout, std::string device_id) {
    t_out = timeout;
    dev_id = device_id; //Если строка не пустая - это будет постоянная подписка
	msqt = mosquitto_new(NULL, true, (void*)(&dev_id));
	if(!msqt){
	    logcpperror<<"Out of memory creating mosquitto client"<<ENDL;
		return;
	}
    mosquitto_log_callback_set(msqt, my_log_callback);
	mosquitto_connect_callback_set(msqt, server_connect_callback);
	mosquitto_disconnect_callback_set(msqt, server_disconnect_callback);
	mosquitto_message_callback_set(msqt, server_message_callback);
	mosquitto_subscribe_callback_set(msqt, server_subscribe_callback);
	logcppdebug<<"mosquitto callbacks have been set"<<ENDL;
	std::string addr;
	int port;
	try {
	   addr = host.substr(host.find("//")+2);
	   size_t pos = addr.find(":");
	   port = atoi(addr.substr(pos+1).c_str());
	   addr = addr.substr(0, pos);
	} catch (...) {
	  logcpperror<<"Error getting mosquitto address."<<ENDL;
	  return;
	}
    logcppdebug<<"msqt addr - "<<addr<<":"<<port<<ENDL;
    if(port>8000) {  //если ssl. нереализовано пока
/*      m_ret = mosquitto_tls_opts_set(msqt, 0, NULL, NULL);
      if(!m_ret ) logcpperror<<mosquitto_strerror(m_ret)<<ENDL;
      char *path_to_ca = ini->GetString(group, (char *)"path_to_ca", false);
      m_ret = mosquitto_tls_set(msqt, NULL, path_to_ca, NULL, NULL, NULL);
      if(!m_ret ) logcpperror<<mosquitto_strerror(m_ret)<<ENDL;
      if(path_to_ca) free(path_to_ca);*/
    }
    int m_ret = mosquitto_connect(msqt, addr.c_str(), port, 60);
	if(m_ret != MOSQ_ERR_SUCCESS) {
		logmessage(ERROR, "Unable to connect to mosquitto server %s:%d. %s", addr.c_str(), port, mosquitto_strerror(m_ret));
		return;
	}
	logcppinfo<<"Mosquitto connected at "<<addr<<":"<<port<<ENDL;
    m_ret = mosquitto_subscribe(msqt, NULL, response_topic.c_str(), 1);
		    if(m_ret != MOSQ_ERR_SUCCESS) {
		      logcpperror<<"Failed subscription to "<<response_topic<<": "<<mosquitto_strerror(m_ret)<<ENDL;
		    } else logcppinfo<<"Subscribing to "<<response_topic<<ENDL;
	waitForResponse.forbid();
    mosquitto_loop_start(msqt);
}

cMosquittoClient::~cMosquittoClient() {
    logcppdebug<<"Cleaning up mosquitto client"<<ENDL;
    mosquitto_loop_stop(msqt, true);
    mosquitto_destroy(msqt);
//    mosquitto_lib_cleanup();
}

// очистка
void mosqCleanUp() {
    mosquitto_lib_cleanup();
}

// выполняет запрос к устройству
void cMosquittoClient::request(std::string action_topic, std::string body) {
    waitForResponse.wait(1); //сначала дождаться подписки на топик статуса, но не более 1с
    if(!action_topic.empty()) {
      logcppinfo<<"Publishing message "<<body<<" to "<<action_topic<<ENDL;
      waitForResponse.forbid();  //Всегда ожидаем
      int m_ret = mosquitto_publish(msqt, NULL, action_topic.c_str(), body.length(), body.c_str(), 1, false);
        if(m_ret!=MOSQ_ERR_SUCCESS) {
           logcpperror<<"Message "<<body<<" to "<<action_topic<<" not sent: "<<mosquitto_strerror(m_ret)<<ENDL;
        }
      waitForResponse.wait(t_out);
    } else {
      logcppwarn<<"Empty topic provided. Not sending."<<ENDL;
    }
}

/*
std::string cMosquittoClient::getResponse(std::string query_topic, std::string body) {
  request(query_topic, body);
  return waitForResponse.getDeviceResponse();
}*/
