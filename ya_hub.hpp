#ifndef YAHUB_HPP_INCLUDED
#define YAHUB_HPP_INCLUDED

#include <mysqlx/xdevapi.h>

#include "iniobject.hpp"
#include "mlogger.hpp"

#define logmessage logger->log
#define logperror(x) logger->log(ERROR, "In %s() line %d. %s: %s", __FUNCTION__,  __LINE__, x, strerror(errno))
#define logcppinfo *logger<<INFO
#define logcppwarn *logger<<WARN
#define logcpperror *logger<<ERROR<<"In "<<__FUNCTION__<<"() line "<<__LINE__<<". "
#define logcppdebug *logger<<DEBUG<<"In "<<__FUNCTION__<<"() line "<<__LINE__<<". "

//коды для manageCommand
#define YAH_NEWLOGIN   1
#define YAH_CHANGEPASS 2
#define YAH_NEWRIGHTS  3
#define YAH_REMOVE     4
#define YAH_GETINFO    5
#define YAH_SITEAUTH   6
#define YAH_FINDLOGIN  9
#define YAH_OK         0
#define YAH_ERROR      50
#define YAH_EXISTS     51
#define YAH_NOTFOUND   52
#define YAH_WRONGPASS  53
#define YAH_NOTACTIVE  54


using namespace mysqlx;

extern cLogger *logger;
//extern struct mosquitto *msqt;
extern bool bCont;
extern SessionSettings *mysqlSettings;
extern std::string mailcmd;
extern std::string deviceResponse;
extern std::string domain;

class cGroupReader {
  public:
      cGroupReader();
      ~cGroupReader();
      void fillKeys(cIniObject *ini, const char *group);
      void fillValues(cIniObject *ini, const char *group);
      void fillAll(cIniObject *ini, const char *group);
      gsize getSize();
      char *getValue(gsize ind);
      char *getKey(gsize ind);
  protected:
    bool checkInd(gsize ind);
    gsize lines_;
    char **keys_;
    char **values_;
};

class cWaitForResponse {
protected:
  bool bReady;
  std::mutex mtx;
public:
  void wait(uint32_t seconds);
  void allow();
  virtual void forbid() {
    mtx.lock(); bReady = false; mtx.unlock();
  }
};

class cWaitAnswer : public cWaitForResponse {
public:
  void setDeviceResponse(std::string resp);// { mtx.lock(); deviceResponse = resp; mtx.unlock(); }
  std::string getDeviceResponse();// { mtx.lock(); std::string ret = deviceResponse; mtx.unlock(); return ret; }
  virtual void forbid() {
    mtx.lock(); bReady = false; deviceResponse.clear(); mtx.unlock();
  }
};
extern cWaitAnswer waitForResponse;


class cMosquittoClient {
private:
  struct mosquitto *msqt;   //Основная структура mosquitto
//  std::string resp_topic;
  std::string dev_id;
  cWaitForResponse wait;
  uint32_t t_out;
public:
  cMosquittoClient(std::string host, std::string response_topic, uint32_t timeout, std::string device_id = "");
  ~cMosquittoClient();
  void request(std::string action_topic, std::string body);
  //std::string getResponse(std::string query_topic, std::string body);
};

class cUserCredentials {
  public:
    char *username;
    char *password;
};
extern cUserCredentials *user_cred;


std::string getVersion();
int manageCommand(int cmd, const char *login, const char *pass, const char *email, bool bSuperUser = false);
bool sendChangePasswordEmail(std::string email, std::string login, std::string active_hex);
RowResult runSQLStatement(std::string sql);
std::string getSQLStringValue(std::string sql);
int getDbFieldSize(std::string table, std::string column);
void cleanUp();
void manageArgv(int argc, char *argv[]);
void calcMemorySize(double &vm_usage, double &resident_set);
bool sendMail(std::string mailto, std::string subject, std::string body);
void getMQTTDeviceState(std::string& device_id);
std::string getDateStringForDB();
void reportDeviceStatus(std::string& device_id);
void mosqCleanUp();
std::string getCurrentWorkingDirectory();
void to_lower_russian(std::string& input);
void trimString(std::string& input);
void resetbCont();
std::string timeToRussianFormat(std::time_t *time);


#endif
