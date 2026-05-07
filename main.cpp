/* ----------------------------------------------------------------------------------------------------------------------------------------------------------------
Универсальный шлюз межуд голосовыми помощниками и умными домами или умными устройствами.

Автор: Ярослав Медокс
----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include <Wt/WApplication.h>
#include <Wt/WServer.h>
#include <Wt/Http/Request.h>
#include <Wt/Http/Client.h>
#include <Wt/Http/Message.h>
#include <Wt/Http/Response.h>
#include <Wt/WContainerWidget.h>
#include <Wt/WText.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WLogger.h>
#include <Wt/WResource.h>
#include <Wt/Json/Value.h>
#include <Wt/Json/Serializer.h>
#include <Wt/Json/Object.h>
#include <Wt/Json/Array.h>
#include <Wt/WLogSink.h>
#include <Wt/WDialog.h>
#include <Wt/WVBoxLayout.h>
#include <Wt/WHBoxLayout.h>
#include <Wt/WMessageBox.h>
#include <Wt/WEnvironment.h>
#include <Wt/WTable.h>
#include <Wt/WTextArea.h>
#include <Wt/WLabel.h>
#include <Wt/WLengthValidator.h>
#include <Wt/WPopupMenu.h>

#include <execinfo.h>


#include <random>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <filesystem>

#include "authwidgets.hpp"
#include "mtimer.hpp"
#include "ya_hub.hpp"
#include "to_words.hpp"

// дефайны для messagbox в классе окна приложения
#define MODE_DEVSAVE   0
#define MODE_DEVDEL    1
#define MODE_ACTIVATE  3
#define MODE_CONSENT   4
#define MODE_DISCOVER  5
#define MODE_REPORT    6
#define MODE_ABOUT     7


bool bCont = true;                  // защелка основного цикла программы
std::mutex main_mtx;                // защищает защелку bCont
std::condition_variable main_cv;    // используется для засыпания основного потока и возможности его принудительного пробуждения


cLogger *logger = nullptr;
uint8_t log_level;
char *msgbox_offset = nullptr;
std::string codeFirst = "";
std::string userFirst = "";
std::string mailcmd = "";
std::string mailto  = "";
std::string device_to_yandex = "";
std::string s_auxlog = "";
/// @brief используется для хранения самого последнего изменившегося апдейта от устройства
std::string idleRequestor = "_idleRequestor_";
double      secondsUnchanged;
SessionSettings *mysqlSettings = nullptr;
const unsigned int bearerLength = 7;


bool parseJsonStringToObject(const std::string& jsonStr, Wt::Json::Object& obj);
void requestDevicesChange();
void setDeviceReporting();

// вспомомогательный класс для отладки работы с запросами.
// достаточно его просто объявить в функции и он выведет параметры и headers из запроса в отдельный лог-файл
class cAuxLog {
private:
  cLogger *auxlog = nullptr;
public:
  cAuxLog(std::string function_, const Http::Request& request) {
    cLogParams p;
    logger->getParams(&p);
    p.max_size_of_file = 30000;
    p.num_files = 3;
    p.tune_sleep = false;  //если берем из существующего логгера - надо запрещать настройку времени простоя
    p.path = (char*)s_auxlog.c_str();  //при инициации копируется значение.
    auxlog = logger_new(&p);
    logcppinfo<<"Creating auxlog"<<ENDL;
    if(auxlog) {
      const Http::ParameterMap pmap = request.getParameterMap();
      *auxlog<<INFO<<function_<<": Request parameters below:"<<ENDL;
      for (auto iter = pmap.begin(); iter != pmap.end(); ++iter) {
        *auxlog<<INFO<<function_<<": "<<std::string(iter->first)<<" = "<<iter->second[0]<<ENDL;
      }
      std::vector< Http::Message::Header > headers = request.headers();
      *auxlog<<INFO<<function_<<": Request headers below:"<<ENDL;
      for (auto iter = headers.begin(); iter != headers.end(); ++iter) {
        *auxlog<<INFO<<function_<<": "<<std::string(iter->name())<<" = "<<iter->value()<<ENDL;
      }
    }
  }
  ~cAuxLog() {
    if(auxlog) {
      logcppinfo<<"Deleting auxlog"<<ENDL;
      delete auxlog;
    }
  }
};


//класс, который хранит все статические (постоянные) подписки mqtt
class cSubscriptions {
private:
  std::vector<cMosquittoClient *> clients;
public:
  cSubscriptions()  { clients.clear(); }
  ~cSubscriptions() { cleanSubscriptions(); }
  bool newSubscription(std::string device_id, std::string host, std::string topic) {
    cMosquittoClient *client = new cMosquittoClient(host, topic, 0, device_id);
    if(client) {
      clients.push_back(client);
    } else {
      logcpperror<<"Couldn't create subsription for "<<topic<<ENDL;
      return false;
    }
    return true;
  }
  void cleanSubscriptions() {
    for(auto client : clients) { delete client; }
    clients.clear();
  }
};
cSubscriptions subscriptions;

// класс, который копит в течении дня количество ошибочных обращений в разрезе каждого устройства.
// используется в ночном отчете и запросе отчета
class cErrors {
private:
  std::map<std::string, uint32_t> errors_;
public:
   void incError(std::string s_device) {
     if(errors_.find(s_device) != errors_.end()) errors_[s_device]++;
     else                                        errors_[s_device] = 1;
   };
   std::string getReport() {
     std::string ret = "";
     for(const auto& [key, value] : errors_) {
        if(!ret.empty()) ret +=", ";
        ret += key + ": " + std::to_string(value) + "\n";
     }
     if(ret.length() == 0) ret = "No errors counted.";
     return ret;
   }
   void cleanUp() { errors_.clear(); };
};
cErrors errors;


// обертка для логгера библиотеки Wt, чтобы перенаправить логирование в собственный логгер
class WLogger_ : public WLogSink {
public:
  virtual void log	(const std::string & type, const std::string & scope, const std::string & message) const noexcept {
    std::string t = type;
    std::transform(t.begin(), t.end(), t.begin(), ::toupper);
    logger->log(logger_getDebugLevelFromChar(t.c_str()), ("Wt. " + message).c_str());  //собственно перенаправление
  }
};
WLogger_ wlogger;


// обертка для параметров, передаваемых при запуске web-сервера в main
class cWServerParameters : public cGroupReader {
  public:
    std::vector<std::string> getParams() {
      std::vector<std::string> ret;
      ret.clear();
      if(!lines_) logcppwarn<<"Parameters for WServer not set"<<ENDL;
      for(gsize i=0;i<lines_;i++) {
        ret.emplace_back("--"+std::string(keys_[i]));
        ret.emplace_back(std::string(values_[i]));
      }
      return ret;
    }
};

// структура параметров внешнего сервиса (Yandex, VK, Salut, Alice или обращения от устройств).
struct externalService {
  std::string requestor;         //имя сервиса. Например, Yandex, VK, Salute
  std::string out_token;         //token для обращения к внешнему сервису
  std::string out_url_state;     //url для отправки статуса устройства внешнему сервису
  std::string out_url_discovery; //url для запроса на запрос от внешнего сервиса на описание устройств(а) (см.протокол Яндекса)
  std::string in_token;          //если сервис только обращается к программе без OAuth 2.0, то нужен токен
  std::string user_ids;          //нужно для универсальных навыков Алисы. Здесь записаны разрешенные пары user_id:requestor_suffix, разделенные запятой
};

// класс, который хранит список client_id от разных экосистем, а также параметры этих экосистем. сделано так, чтобы различать запросы от яндекса и вк
// client_id - дифференциатор разных сервисов, даже если он не используется самим сервисом
class cClientIds {
  private:
    std::map<std::string, externalService> services;
    size_t pos;
  public:
    void fillData(cIniObject *ini) {
      cGroupReader reader;
      reader.fillAll(ini, (const char*)"Client_ids");
      if(reader.getSize() == 0) {
        logcpperror<<"Client ids group is not set up. Exiting."<<ENDL;
        resetbCont();
        return;
      }
      for(gsize i=0;i<reader.getSize();i++) {
        externalService service;
        service.requestor         = std::string(reader.getValue(i));
        service.out_token         = std::string(ini->getString(reader.getKey(i), (char *)"out_token",         (char *)""));
        service.out_url_state     = std::string(ini->getString(reader.getKey(i), (char *)"out_url_state",     (char *)""));
        service.out_url_discovery = std::string(ini->getString(reader.getKey(i), (char *)"out_url_discovery", (char *)""));
        service.in_token          = std::string(ini->getString(reader.getKey(i), (char *)"in_token",          (char *)""));
        service.user_ids          = std::string(ini->getString(reader.getKey(i), (char *)"user_ids",          (char *)""));
        services.insert({std::string(reader.getKey(i)), service});
      }
      std::string sql = "delete from statuses where requestor NOT in (";
      for(auto [id, service] : services) {
        logcppdebug<<id<<" = "<<service.requestor<<ENDL;
        sql += "'"+service.requestor+"', " ;
      }
      sql += "'"+idleRequestor+"')";
      logcppwarn<<"Cleaning statuses for non-existing services."<<ENDL;
      runSQLStatement(sql);
    }
    bool isAuthorized(std::string& client_id) {
      if(auto service=services.find(client_id); service!=services.end()) return true;
      return false;
    }
    bool isAuthorizedByUserId(std::string user_id, std::string& requestor) {
       for(auto [id, service] : services) {
         if(service.user_ids.empty()) { continue; }
         else {
           size_t start = 0;
           while (start < service.user_ids.size()) {
             size_t commaPos = service.user_ids.find(',', start);
             std::string tmp;
             if (commaPos == std::string::npos) {
                tmp = service.user_ids.substr(start);
                start = service.user_ids.size(); // выход из цикла, даже если не нашли пользователя
             } else {
                tmp = service.user_ids.substr(start, commaPos);
                start = commaPos + 1;
             }
             size_t delimPos  = tmp.find(':', 0);
             std::string u_id = tmp.substr(0, delimPos);
             trimString(u_id);
             if(u_id == user_id) {
                requestor = tmp.substr(delimPos+1);
                trimString(requestor);
                requestor = service.requestor+requestor;
                return true;
             }
           }
         }
       }
       return false;
    }
    externalService* isAuthorizedByToken(std::string& token) {
       if(token.empty()) return nullptr;
       for(auto [id, service] : services) {
         if(service.in_token == token) { return &service; }
       }
       return nullptr;
    }
    std::string getRequestor(std::string& client_id) {
      if(auto service = services.find(client_id); service!=services.end()) { return service->second.requestor; }
      logcpperror<<"Requestor for "<<client_id<<" not found!"<<ENDL;
      return "";
    }
    void rewind() { pos = 0; }
    externalService *getNext(std::string& client_id) {
      auto it = services.begin();
      if(pos < services.size()) {
        std::advance(it, pos++); // перемещаем итератор на заданный индекс
        client_id = it->first;
        return &(it->second);
      }
      return nullptr;
    }
};
cClientIds clientIds;

// запуск логирования. Параметры не по умолчанию!
void initLogger(cIniObject *ini) {
   if(!logger) {
      cLogParams p;
      char *group  = (char *)"Logger";
      p.max_messages     = ini->getInt(   group, (char *)"mmax", 100);
      p.max_size_of_file = ini->getInt(   group, (char *)"fsize", 1000000);
      p.path             = ini->getString(group, (char *)"fpath");
      p.num_files        = ini->getInt(   group, (char *)"fnum", 3);
      p.sink_cout        = true;  //можно отключ
      p.sink_log         = true;
      p.tune_sleep       = true;  //этот логгер основной и имеет право регулировать скорость.
      p.log_level        = logger_getDebugLevelFromChar(ini->getString(group, (char *)"level"));
      log_level = p.log_level;    //сохраняем на случай динамической смены режима
      logger = logger_new(&p);
      logger_setSleepTime(ini->getInt(group, (char *)"stime", 10000));
      if(!logger) {
        exit(-1);
      }
      logcppinfo<<"ya_hub logger started with level "<<c_level[p.log_level]<<ENDL;
   }
}


// инициализация параметров mySQL. Используются потом при обращении к базе.
void initMySQL(cIniObject *ini) {
   const char *group  = "MySQL";
   if(mysqlSettings) { delete mysqlSettings; mysqlSettings = nullptr; }
   if(!(ini->hasGroup(group))) {
     logcpperror<<"MySQL settings absent in configuration file"<<ENDL;
     mysqlSettings = nullptr;
     return;
   }
   try {
      mysqlSettings = new SessionSettings(SessionOption::HOST, ini->getString(group, (char *)"host"),
                                          SessionOption::PORT, ini->getInt(   group, (char *)"port", 33060),
                                          SessionOption::USER, ini->getString(group, (char *)"user"),
                                          SessionOption::PWD,  ini->getString(group, (char *)"pass"),
                                          SessionOption::DB,   ini->getString(group, (char *)"db"));
                                          //SessionOption::SSL_MODE, SSLMode::DISABLED);   // <<< это на Samsung без этого не работает
      Session mySession(*mysqlSettings);
      logcppdebug<<"MySQL session started"<<ENDL;
      Schema myDB = mySession.getDefaultSchema();
      logcppdebug<<"Schema selected "<<myDB.getName()<<ENDL;
      return;
    } catch(const mysqlx::Error &err) {
       logcpperror<<err<<ENDL;
    }
    mysqlSettings = nullptr;
    resetbCont();
}

// для удобства, возвращает строковые значения из ini-файла
void readStringValue(cIniObject *ini, const char *group, const char *key, std::string& value) {
  char *m = ini->getString(group, key, false);
  if(m) value = std::string(m); else value.clear();
}

// Главная унифицированная функция для чтения параметров и инициализации всего.
// Пока нельзя повторно вызывать. Надо перезапускать программу.
void readValues(char *ini_fname, cWServerParameters *sparams) {
  cIniObject ini(ini_fname);
  //IniCipher ini(ini_fname, cipher_key);
  initLogger(&ini);
  logcppdebug<<"logger init finished"<<ENDL;
  initMySQL(&ini);
  logcppdebug<<"mySQL init finished"<<ENDL;
  sparams->fillAll(&ini, (char *)"WServer");
  logcppdebug<<"WServer settings filled"<<ENDL;
  const char *group = "Settings";
  msgbox_offset = ini.getString(group, "msgbox_offset", false);
  readStringValue(&ini, group, (char*)"mailcmd", mailcmd);
  readStringValue(&ini, group, (char*)"mailto", mailto);
  secondsUnchanged = ini.getDouble(group, (char*)"secondsUnchanged", 600);
  readStringValue(&ini, group, (char*)"auxlog", s_auxlog);
  readStringValue(&ini, group, (char*)"domain", domain);
  logcppdebug<<"Settings section done"<<ENDL;
  clientIds.fillData(&ini);
  logcppdebug<<"Client ids done"<<ENDL;
}


// две функции для безопасной проверки продолжения работы, приостановки, если надо и остановки
bool getbCont() {
  bool bRet;
  main_mtx.lock();
  bRet = bCont;
  main_mtx.unlock();
  return bRet;
}
void resetbCont(){
  main_mtx.lock();
  bCont = false;
  main_mtx.unlock();
}

// перехватчик SIGINT
void sigint_handler(int i)
{
   printf("\n");
   logcppinfo<<"Terminating program via Ctrl+C."<<ENDL;
   main_cv.notify_one(); // Будим принудительно один поток
   resetbCont();
}

// Поскольку выход из программы возможен в нескольких местах, все деструкторы собраны в одном месте.
// Вызывается перед выходом из программы
void cleanUp() {
      waitForResponse.allow();
      timer_killall();
      errors.cleanUp();
      mosqCleanUp();
      if(logger) delete logger;
}


// единая функция получения списка устройств из базы данных
RowResult getDevicesFromDatabase(std::string order = "") {
  std::string sql = "select id_for_yandex, convert(device using utf8) as dev from devices" + (order.length()==0?"":(" order by "+order));
  return runSQLStatement(sql);
}

// дополнение к моей библиотеке authwidgets
class AuthSession : public AuthWidgets {
public:
  bool findLogin(std::string login) override {
    if(manageCommand(YAH_FINDLOGIN, login.c_str(), "", "") == YAH_OK) return true;
    return false;
  };
  bool checkLogin(std::string login, std::string password, std::string& msg) override {
    int ret = manageCommand(YAH_SITEAUTH, login.c_str(), password.c_str(), "");
    if(ret == YAH_OK) return true;
    if(ret == YAH_NOTACTIVE) msg = "Логин не активирован. см.эл.почту";
    if(ret == YAH_WRONGPASS) msg = "Логин/пароль неверные";
    return false;
  };
  bool saveLogin(std::string login, std::string password, std::string email) override {
    //if(manageCommand(YAH_NEWLOGIN, login.c_str(), password.c_str(), email.c_str()) == YAH_OK) return true;
    return false;
  };
  bool restoreLogin(std::string email) override {
    RowResult res = runSQLStatement("select username, active_hex from account where email='"+email+"'");
      if(res.count()) {
        auto rows = res.fetchAll();
        for(auto row : rows ) {
          if(!sendChangePasswordEmail(email, std::string(row[0]), std::string(row[1]))) return false;
        }
        return true;
      } else {
        logcppwarn<<"No account found with email: "<<email<<ENDL;
      }
    return false;
  };
  bool changePassword(std::string login, std::string password) override {
    if(manageCommand(YAH_CHANGEPASS, login.c_str(), password.c_str(), "") == YAH_OK) return true;
    return false;
  }
};

// Генерация токена
std::string generateToken() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t r1 = dis(gen), r2 = dis(gen);
    std::ostringstream oss;
    oss << std::hex << r1 << r2;
    return oss.str();
}

// из исходной строки берет значение по ключу, как если бы строка была настощим Json-объектом. Собственно она и становится им.
std::string getFieldFromDevice(std::string device, std::string field) {
  Json::Object tmp;
  if(parseJsonStringToObject(device, tmp)) {
    if(tmp.contains(field)) return std::string(tmp[field]);
  }
  return field + "=NULL";
}

// возвращает часть пути по номеру узла
std::string getUrlTreeValue(std::string url, int num) {
    size_t pos1 = url.find("/");
    if(pos1 == 0) pos1++; else pos1 = 0;  //на тот случай, если путь не начинается с "/"
    size_t pos2 = url.find("/", pos1);
    if(num>0 && pos2 == std::string::npos) return url.substr(pos1);
    int i = 0;
    while(i < num) {
      pos1 = url.find("/", pos2) + 1 ;
      pos2 = url.find("/", pos1);
      i++;
      if(pos2 == std::string::npos) {
        if(i == num) return url.substr(pos1);
        else {
          logcpperror<<"too big number passed: "<<num<<ENDL;
          return "";
        }
      }
    }
    return url.substr(pos1, pos2-pos1);
}

// Прооверка является ли пользователь админом. На самом деле в этом приложении только 1 пользователь (невозможно создать второго и он админ
bool isAdmin(std::string login) {
  RowResult rows = runSQLStatement("Select superuser from account where username ='"+login+"'");
  if(rows.count()) {
    Row row = rows.fetchOne();
    return bool(row[0]);
  }
  return false;
}


// Интерфейсная функция. Запуск возможен в обычном режиме и в режиме авторизации только для OAuth 2.0
class yhApplication : public WApplication {
public:
  explicit yhApplication(const WEnvironment& env) : WApplication(env) {
    setTitle("YM Hub");
    bChanged = false;
    container = root()->addWidget(std::make_unique<WContainerWidget>());
    container->clear();
    container->setStyleClass("table-list");
    useStyleSheet("/css/list.css");
    try {
    const Http::ParameterMap pmap = env.getParameterMap();
    for (auto iter = pmap.begin(); iter != pmap.end(); ++iter) {
      logcppdebug<<std::string(iter->first)<<" = "<<iter->second[0]<<ENDL;
      setOAuthState(iter->first, iter->second[0]);
    }
    } catch (WServer::Exception& e) {
      logcpperror << "Error starting Wt application server: "<< e.what() << ENDL;
      quit();
      return;
    } catch (std::exception& e) {
      logcpperror << "exception: " << e.what() << ENDL;
      quit();
      return;
    }
    if(!env.getParameterValues("changepass").empty()) {
      showAuth(getSQLStringValue("select username from account where active_hex='"+env.getParameterValues("changepass")[0]+"'"));
      return;
    }
    if(!env.getParameterValues("activate").empty()) {
      activateAccount(env.getParameterValues("activate")[0]);
      return;
    }
    if(sessionData.find("redirect_uri")!=sessionData.end()) { //признак того, что идет запрос авторизации от Яндекса
      if (!clientIds.isAuthorized(sessionData["client_id"]) || sessionData["response_type"] != "code") {
          container->addWidget(std::make_unique<WText>("<h1>Ошибка авторизации!</h1>"));
          logcpperror<<"Authorization error (client_id or response_type wrong)"<<ENDL;
          quit();
          return;
        }
#ifdef __RELEASE__
    } else {
      if(sessionData.size()) { quit(); return; } //все что с какими-то "левыми" параметрами сразу отправляем в "сад"
#endif // __DEBUG__
    }
    globalKeyWentDown().connect(this, &yhApplication::handleKeyDown);
    showAuth();
    if(user_cred) {
      WLineEdit *le = (WLineEdit *)authw->find("login");    le->setText(user_cred->username);
                 le = (WLineEdit *)authw->find("password"); le->setText(user_cred->password);
                 le->setFocus();
                 authw->submit();
      delete user_cred;
      user_cred = nullptr;
    }
  }
  ~yhApplication() { cleanApp(); }
  void activateAccount(std::string active_hex) {
    std::string err = "Error activating account "+active_hex+":";
    logcppdebug<<"activating "<<active_hex<<ENDL;
    try {
      Session mySession(*mysqlSettings);
      Schema myDB   = mySession.getDefaultSchema();
      Table acc     = myDB.getTable("account");
      RowResult res = acc.select("status", "username").where("active_hex = :a_hex").bind("a_hex", active_hex).execute();
      if(res.count()) {
        auto row = res.fetchOne();
        if(int64_t(row[0]) == 0) {
          acc.update().set("status", 1).where("active_hex = :a_hex").bind("a_hex", active_hex).execute();
          messageBox("Аккаунт активирован.", "", "", "Ok", MODE_ACTIVATE, "");
          logcppinfo<<"Account "<<row[1]<<" activated."<<ENDL;
        } else {
          messageBox("Аккаунт уже активирован.", "", "", "Ok", MODE_ACTIVATE, "");
        }
      } else {
        messageBox("Аккаунт не найден.", "", "", "Ok", MODE_ACTIVATE, "");
      }
    } catch(const mysqlx::Error &e) {
      logcpperror<<err<<e.what()<<ENDL;
      return;
    } catch(const std::exception &e) {
      logcpperror<<err<<e.what()<<ENDL;
      return;
    }
  }
  void showAuth(std::string login = "") {
    bool bShow = (login.length()==0);
    authw = root()->addWidget(std::make_unique<AuthSession>());
    authw->loginDone().connect(this, &yhApplication::loginDone);
    authw->setVOffset(msgbox_offset);
    if(bShow) authw->showAuth();
    else      authw->showChangePass(login);
  }
  void cleanApp(){
    logcppinfo<<"cleaning yhApp"<<ENDL;
    login_.clear();
  }
  void loginDone(std::string login) {
    login_ = login;
    bAdmin = isAdmin(login_.c_str());
    if(authw) {
      authw->clear();
      root()->removeChild(authw);
    }
    removeStyleSheet("/css/auth.css");
    authw = NULL;
    if(sessionData.find("redirect_uri")!=sessionData.end()) { //Это означает, что вход был через запрос авторизации от Яндекса.
      logcppdebug<<"Starting OAuth procedures"<<ENDL;
      messageBox("Вы подтверждаете предоставление доступа к вашему аккаунту облачного провайдера?", "Разрешить", "", "Отклонить", MODE_CONSENT, "");
    }
    internalPathChanged().connect(this, &yhApplication::handleInternalPath);
    setInternalPath("/list",  true);
  }
  void addBottomLinks(bool bList) {
    container->addWidget(std::make_unique<WBreak>());
    if(bList) {
      auto link1 = container->addWidget(std::make_unique<WAnchor>("/list", "обновить"));
      link1->setLink(WLink(LinkType::InternalPath, "/list"));
      if(bAdmin) {
        auto link3 = container->addWidget(std::make_unique<WAnchor>("/device/newdevice", "добавить устройство"));
        link3->setLink(WLink(LinkType::InternalPath, "/device/newdevice"));
      }
    } else {
      auto link1 = container->addWidget(std::make_unique<WAnchor>("/list", "вернуться к списку"));
      link1->setLink(WLink(LinkType::InternalPath, "/list"));
    }
    WAnchor *link1 = (WAnchor *)container->children()[container->count()-1];
    link1->setPadding(WLength("10px"), Side::Left|Side::Right);
    auto link2 = container->addWidget(std::make_unique<WAnchor>("/", "ВЫЙТИ"));
    link2->setLink(WLink(LinkType::InternalPath, "/"));
    link2->setPadding(WLength("10px"), Side::Left|Side::Right);
  }
  void showList() {
    int row = 0;
    if(!container) { container = root()->addWidget(std::make_unique<WContainerWidget>()); }
    container->clear();
    container->setStyleClass("table-list");
    setInternalPath("/list", false);
    std::string caption = "Список устройств пользователя <b>" + login_;
    RowResult res = getDevicesFromDatabase("id_for_yandex");//runSQLStatement("select id_for_yandex, convert(device using utf8) as dev from devices order by id_for_yandex");
    if(res.count()) {
      caption += ":</b>";
      container->addWidget(std::make_unique<WText>(caption));
      auto tbl = container->addWidget(std::make_unique<WTable>());
      int col_ind = 0;
      tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WText>("<i>Устройство</i>"));
      tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WText>("<i>Комната</i>"));
      tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WText>("<i>Описание</i>"));
      for(Row sqlrow=res.fetchOne();sqlrow;sqlrow=res.fetchOne()) {
        row++;
        col_ind = 0;
        std::string device_id = std::string(sqlrow[0]);
        if(bAdmin) {
          auto link = tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WAnchor>("/device/"+device_id, device_id));
          link->setLink(WLink(LinkType::InternalPath, "/device/"+device_id));
        } else {
          tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WText>(device_id));
        }
        tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WText>(getFieldFromDevice(std::string(sqlrow[1]), "room")));
        tbl->elementAt(row, col_ind++)->addWidget(std::make_unique<WText>(getFieldFromDevice(std::string(sqlrow[1]), "description")));
      }
    } else {
      caption += "</b> пуст!";
      logcppdebug<<"No data: "<<ENDL;
    }
    addBottomLinks(true);
    auto popup = std::make_unique<Wt::WPopupMenu>();
    popup->setStyleClass("Wt-popupmenu");
    auto item = popup->addItem("W");
    item->triggered().connect(this, &yhApplication::toggleLogLevel);
    item->setObjectName("log");
    popup->addItem("Показать отчет")->triggered().connect(this, &yhApplication::showReport);
    popup->addItem("Бэкап описаний устройств")->triggered().connect(this, &yhApplication::saveBackup);
    popup->addItem("О программе")->triggered().connect(this, &yhApplication::showAbout);
    popup->setHideOnSelect(true);
    auto btn = container->addWidget(std::make_unique<WPushButton>("действия"));
    btn->setMenu(std::move(popup));
    btn->setStyleClass("btndrop");
    setMenuLogName();
  }
  void saveBackup() {
    std::string outputFile = getCurrentWorkingDirectory()+"/backup.txt";
    std::ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        logcpperror << "Error opening file to write backup: " << outputFile << ENDL;
        return;
    }
    RowResult res = getDevicesFromDatabase();
    while (Row row = res.fetchOne()) {
        // Записываем ID
        outFile << "id_for_yandex: " << row[0] << "\n";
        // Записываем описание устройства (предполагаем, что это текст в UTF-8)
        outFile << "device: " << row[1] << "\n";
        outFile << "\n" << std::string(50, '-') << "\n"; // Разделитель между записями
    }
    outFile.close();
    logcppinfo<<"Backup file "<<outputFile<<" saved."<<ENDL;
  }
  void handleKeyDown(const Wt::WKeyEvent& event) {
        if (event.key() == Wt::Key::Escape) {
          if (internalPathMatches("/device")) setInternalPath("/list", true);
        }
  }
  void showReport() {
    messageBox(errors.getReport(), "", "", "Понятно", MODE_REPORT, "");
  }
  void showAbout() {
    messageBox(getVersion(), "", "", "Ok", MODE_ABOUT, "");
  }
  void setMenuLogName() {
    cLogParams p;
    logger->getParams(&p);
    std::string txt = "Логировать с уровнем " + std::string(c_level[p.log_level!=log_level?log_level:DEBUG]);
    WMenuItem *item = (WMenuItem *)findWidget("log");
    if(item) item->setText(txt);
  }
  void toggleLogLevel() {
    cLogParams p;
    logger->getParams(&p);
    if(log_level != p.log_level) { //возврат к настроенном в conf-файле
      p.log_level = log_level;
    } else {
      if(log_level != DEBUG) { p.log_level = DEBUG; } //переключаемся только если в настройках не установлен изначально DEBUG
    }
    logger->setParams(&p);
    logcppinfo<<"User "<<login_<<" changed log level to "<<c_level[p.log_level]<<ENDL;
    setMenuLogName();
  }
  void showDevice(bool bClone = false) {
    container->clear();
    bChanged = false;
    std::string device_id = getUrlTreeValue(internalPath(), 1);
    bool bNewDevice = device_id.find("newdevice") != std::string::npos;
      int row = 0;
      auto tbl = container->addWidget(std::make_unique<WTable>());
      tbl->setObjectName("Table");
      container->setStyleClass("table-caps");
      auto wtxt = tbl->elementAt(row,1)->addWidget(std::make_unique<WText>());
      wtxt->setObjectName("msg");
      wtxt->decorationStyle().setForegroundColor(WColor("red"));
      row++;
      auto label = tbl->elementAt(row,0)->addWidget(std::make_unique<WLabel>("<b>Id устройства:</b>"));
      if(bNewDevice) {
        auto le = tbl->elementAt(row,1)->addWidget(std::make_unique<WLineEdit>(device_id));
        le->setObjectName("ledevice_id");
        int max_size = getDbFieldSize("devices", "id_for_yandex");
        le->keyWentDown().connect(this, &yhApplication::handleKeyDown);
        std::shared_ptr<WLengthValidator> val = std::make_shared<WLengthValidator>(2, max_size);
        val->setMandatory(true);
        le->setValidator(val);
        le->textInput().connect(this, &yhApplication::setChanged);
        label->setBuddy(le);
      } else {
        tbl->elementAt(row,1)->addWidget(std::make_unique<WText>(device_id))->setObjectName("txdevice_id");
      }
      row++;
      label = tbl->elementAt(row,0)->addWidget(std::make_unique<WLabel>("Описание устройства:"));
      auto tarea = tbl->elementAt(row,1)->addWidget(std::make_unique<WTextArea>(getSQLStringValue("select convert(device using utf8) as dev from devices where id_for_yandex='"+device_id+"'")));
      tarea->setObjectName("json");
      tarea->keyWentDown().connect(this, &yhApplication::handleKeyDown);
      tarea->textInput().connect(this, &yhApplication::setChanged);
      label->setBuddy(tarea);
      row++;
      auto butt = tbl->elementAt(row,0)->addWidget(std::make_unique<WPushButton>("Сохранить"));
      butt->clicked().connect(this, &yhApplication::buttonSaveVoid);
      butt->setToolTip("Сохранить устройство");
      butt->setEnabled(false);
      butt->setObjectName("save");
      butt = tbl->elementAt(row,1)->addWidget(std::make_unique<WPushButton>("Удалить"));
      butt->setToolTip("Удалить устройство "+device_id);
      butt->clicked().connect(this, std::bind(&yhApplication::messageBox, this, "Удаление устройства", "Удалить", "", "Отмена", MODE_DEVDEL, ""));
      if(!bNewDevice) {
        butt = tbl->elementAt(row,1)->addWidget(std::make_unique<WPushButton>("Клонировать"));
        butt->setToolTip("Клонировать устройство "+device_id);
        butt->clicked().connect(this, std::bind(&yhApplication::showClone, this));
      }
      butt = tbl->elementAt(row,1)->addWidget(std::make_unique<WPushButton>("в Яндекс"));
      butt->setToolTip("Обновление всех устройств в Яндекс "+device_id);
      butt->clicked().connect(this, std::bind(&yhApplication::messageBox, this, "Отправить в Яндекс", "Все", "Это", "Отмена", MODE_DISCOVER, ""));
      butt->setObjectName("yandex");
    addBottomLinks(false);
  }
  void showClone() {
    std::string device_id = getDeviceIdFromEdit();
    std::string json = getUTF8_TA("json");
    container->clear();
    setInternalPath("device/newdevice");
    showDevice();
    WLineEdit *le = (WLineEdit *)container->find("ledevice_id");
    WTextArea *ta = (WTextArea *)container->find("json");
    if(le) le->setText(device_id);
    if(ta) ta->setText(json);
    setChanged();
  }
  void setChanged() {
    bChanged = true;
    buttSaveEnabled(true);
    buttYandexEnabled(false);
  }
  bool checkJSON() {
    WTextArea * tarea = (WTextArea *)container->find("json");
    std::string json = tarea->valueText().toUTF8();
    Json::Object result;
    if(parseJsonStringToObject(json, result)) {
      setMessageText("");
      tarea->label()->decorationStyle().setForegroundColor(WColor());
      return true;
    }
    setMessageText("Не парсится Json");
    tarea->label()->decorationStyle().setForegroundColor(WColor("red"));
    return false;
  }
  void putMessage(WLineEdit *edit, const WString text) {
    setMessageText(text);
    edit->label()->decorationStyle().setForegroundColor(WColor("red"));
  }
  void clearMessage(WLineEdit *edit) {
    setMessageText("");
    edit->label()->decorationStyle().setForegroundColor(WColor());
  }
  bool checkValid(WLineEdit *edit, const WString& text) {
    if (edit->validate() != ValidationState::Valid) {
      putMessage(edit, text);
      return false;
    } else {
      clearMessage(edit);
      return true;
    }
  }
  bool checkUniqueId(WLineEdit *edit, const WString& text) {
    if(edit->valueText().toUTF8() == idleRequestor) {
      putMessage(edit, idleRequestor + " зарезервированное имя");
      return false;
    }
    if (!getSQLStringValue("select id_for_yandex from devices where id_for_yandex='"+edit->valueText().toUTF8()+"'").empty()) {  //нашли такой id, запрет
      putMessage(edit, text);
      return false;
    } else {
      clearMessage(edit);
      return true;
    }
  }
  inline std::string getUTF8_T(std::string field) {
     WText *wt = (WText *)(container->find(field));
     return wt->text().toUTF8();
  }
  inline std::string getUTF8_L(std::string field) {
     WLineEdit *wl = (WLineEdit *)(container->find(field));
     return wl->valueText().toUTF8();
  }
  inline std::string getUTF8_TA(std::string field) {
     WTextArea *wa = (WTextArea *)(container->find(field));
     return wa->valueText().toUTF8();
  }
  std::string getDeviceIdFromEdit() {
    return container->find("txdevice_id")?getUTF8_T("txdevice_id"):getUTF8_L("ledevice_id");
  }
  bool saveDevice() {
    std::string save_err = "Error saving device: ";
    std::string device_id = getDeviceIdFromEdit();
    std::string json = getUTF8_TA("json");
    try {
      Session mySession(*mysqlSettings);
      Schema myDB = mySession.getDefaultSchema();
      Table cap  = myDB.getTable("devices");
      RowResult res = cap.select("device")
                         .where("id_for_yandex=:i_d")
                         .bind("i_d", device_id).execute();
      if(res.count()) { //будем делать апдейт
         cap.update()
            .set("device", json)
            .where("id_for_yandex=:i_d")
            .bind("i_d", device_id).execute();
      } else {          //будем сохранять новую запись
         cap.insert("id_for_yandex", "device")
            .values(device_id, json).execute();
      }
    } catch(const mysqlx::Error &err) {
      logcpperror<<save_err<<err.what()<<ENDL;
      return false;
    } catch(const std::exception &err) {
      logcpperror<<save_err<<err.what()<<ENDL;
      return false;
    }
    setDeviceReporting(); //при каждом сохранении устройства пересоздаются таймеры.
    return true;
  }
  bool deleteDevice() {
    std::string del_err = "Error deleting device: ";
    std::string device_id = getDeviceIdFromEdit();
    try {
      Session mySession(*mysqlSettings);
      Schema myDB = mySession.getDefaultSchema();
      Table cap  = myDB.getTable("devices");
      cap.remove().where("id_for_yandex=:i_d").bind("i_d", device_id).execute();
      setDeviceReporting(); //при каждом удалении устройства пересоздаются таймеры.
      return true;
    } catch(const mysqlx::Error &err) {
      logcpperror<<del_err<<err.what()<<ENDL;
    } catch(const std::exception &err) {
      logcpperror<<del_err<<err.what()<<ENDL;
    }
    return false;
  }
  void buttSaveEnabled(bool bEnable) {
    WPushButton *b = (WPushButton *)container->find("save");
    b->setEnabled(bEnable);
  }
  void buttYandexEnabled(bool bEnable) {
        WPushButton *b = (WPushButton *)container->find("yandex");
        if(b) b->setEnabled(bEnable);
  }
  bool buttonSave() {
    if(bChanged) {
      bool bRet = true;
      WLineEdit *le = (WLineEdit *)container->find("ledevice_id");
      if(le) {  //это новое устройство
        if(!checkValid(le, "Некорректная длина id устройства"))  { bRet = false; }
        if(!checkUniqueId(le, "Устройство с таким id уже есть")) { bRet = false; }
        if(bRet) clearMessage(le);
      }
      if(bRet && !checkJSON()) { bRet = false; }
      //прошли проверки - сохраняем
      if(bRet && !saveDevice()) {
        bRet = false;
      }
      if(bRet) {
        bChanged = false;
        buttYandexEnabled(true);
        buttSaveEnabled(false);
      } //else handleInternalPath(internalPath());
    } else {
      setMessageText("Не было изменений");
    }
    return true;
  }
  void buttonSaveVoid() {
    if(buttonSave()) {;} //setInternalPath("/list", true);
  }
  inline void setMessageText(WString text) {
    WText *w = (WText *)(container->find("msg"));
    w->setText(text);
  }
  void handleInternalPath(const std::string &path) {
    if(bChanged) {
      messageBox("Данные измененены!", "Сохранить", "Сбросить", "Продолжить", MODE_DEVSAVE, "");
      return;
    }
    savedPath_ = path;
    if (internalPathMatches("/list"))   { showList();   return; }
    if (internalPathMatches("/device")) { showDevice(); return; }
    quit();
    redirect("/");
  }
  void handleConsentDenied() {
      redirect("/");
      usleep(100000L);
      quit();
  }
  void handleConsentGranted() {
    std::string redirectUri = getOAuthState("redirect_uri");
    std::string state = getOAuthState("state");
    codeFirst  = generateToken();
    userFirst  = login_;
    // Формируем URL для редиректа на Яндекс
    std::string oauthUrl = redirectUri + "?code=" + codeFirst + "&state=" + state;
    logcppinfo<<"Redirecting authorization to: "<<oauthUrl<<ENDL;
    // Выполняем редирект
    redirect(oauthUrl);
    usleep(1000000L);
    quit();
  }
  void messageBoxDone(int mode, std::string param) {
    StandardButton result = messageBox_->buttonResult();
    if(mode == MODE_ACTIVATE) {
      quit();
      redirect("/");
    }
    if(mode == MODE_DISCOVER) {
      switch (result) {
        case StandardButton::Ok: //удалить
          requestDevicesChange();
          break;
        case StandardButton::No: //не сохранять и перейти
          device_to_yandex = getDeviceIdFromEdit();
          requestDevicesChange();
          break;
        case StandardButton::Cancel:     //продолжить редактирование
          setInternalPath(savedPath_, false);
          break;
        default:
          break;
      }
    }
    if(mode == MODE_CONSENT) {
       switch (result) {
        case StandardButton::Ok: //сохранить
          handleConsentGranted();
          break;
        default:
          handleConsentDenied();
          break;
       }
    }
    if(mode == MODE_DEVSAVE) {
      WTextArea *ta = (WTextArea *)container->find("json");
      switch (result) {
        case StandardButton::Ok: //сохранить
          //if(buttonSave()) handleInternalPath(internalPath());
          //else             setInternalPath(savedPath_, false);
          if(buttonSave()) {};
          break;
        case StandardButton::No: //не сохранять и перейти
          bChanged = false;
          handleInternalPath(internalPath());
          break;
        case StandardButton::Cancel:     //продолжить редактирование
          setInternalPath(savedPath_, false);
          if(ta) ta->setFocus();
          break;
        default:
          break;
      }
    }
    if(mode == MODE_DEVDEL) {
      switch (result) {
        case StandardButton::Ok: //удалить
          if(deleteDevice()) setInternalPath("/list", true);//handleInternalPath(internalPath());
          else               setInternalPath(savedPath_, false);
          break;
        case StandardButton::Cancel:     //продолжить редактирование
          setInternalPath(savedPath_, false);
          break;
        default:
          break;
      }
    }
    if(mode == MODE_REPORT || mode == MODE_ABOUT) { //ничего не делаем
    }
    messageBox_.reset();
  }
  void messageBox(std::string text, std::string butt1, std::string butt2, std::string butt3, int mode, std::string param) {
    messageBox_ = std::make_unique<WMessageBox>("Внимание!", text, Icon::Warning, StandardButton::None);
    if(butt1.length()) messageBox_->addButton(butt1, StandardButton::Ok);
    if(butt2.length()) messageBox_->addButton(butt2, StandardButton::No);
    WPushButton *continueButton = messageBox_->addButton(butt3, StandardButton::Cancel);
    messageBox_->setDefaultButton(continueButton);
    messageBox_->buttonClicked().connect(this, std::bind(&yhApplication::messageBoxDone, this, mode, param));
    messageBox_->setStyleClass("messagebox");
    //messageBox_->setIcon(Icon::Warning);
    messageBox_->setOffsets(WLength(msgbox_offset), Side::Top);
    //messageBox_->animateShow(WAnimation(AnimationEffect::Pop | AnimationEffect::Fade, TimingFunction::Linear, 100));
    messageBox_->show();
  }
private:
  AuthSession *authw;
  WContainerWidget *container;
  bool bAdmin, bChanged;
  std::unique_ptr<Wt::WMessageBox> messageBox_;
  std::string savedPath_;
  std::string login_;
  std::map<std::string, std::string> sessionData;
  void setOAuthState(std::string key, std::string value) {
    sessionData.insert(std::make_pair(key, value));
  }
  std::string getOAuthState(std::string key) {
    return sessionData[key];
  }
};

// получает и обрезает если надо токен из входящего запроса. Обрезать значит убрать "Bearer "
std::string getAccessToken(const Http::Request& request, bool bFull) {
  try {
    std::string ret = request.headerValue("Authorization");
    if(bFull) return ret; else return ret.substr(bearerLength);
  } catch(...) {
    logcpperror<<"Couldn't get token from request header"<<ENDL;
    return "";
  }
}

// Класс для удобства работы с токеном моего приложения. Токен всегда один для каждого client_id
class cActiveToken {
public:
  void saveToken(std::string token, std::string refresh, std::string username, std::string client_id) {
    deleteTokenById(client_id);
    runSQLStatement("insert into tokens (token, refresh, username, client_id) values ('" + token + "', '" + refresh + "', '" + username + "', '"+client_id+"')");
  }
  bool isAuthorized(const std::string& token, std::string& username) {
    username = getSQLStringValue("select username from tokens where token='"+token+"'");
    return username.empty()?false:true;  //если строка найдена, значит такой токен есть
  }
  std::string getRefreshToken(const std::string& client_id) {
      return getSQLStringValue("select refresh from tokens where client_id='"+client_id+"'");
  }
  std::string getUserById(const std::string& client_id) {
      return getSQLStringValue("select username from tokens where client_id='"+client_id+"'");
  }
  std::string getUserByToken(const std::string& token) {
      return getSQLStringValue("select username from tokens where token='"+token+"'");
  }
  void deleteTokenById(const std::string& client_id) {
    runSQLStatement("delete from tokens where client_id='"+client_id+"'");
  }
  void deleteTokenByToken(const std::string& token) {
    runSQLStatement("delete from tokens where token='"+token+"'");
  }
};
cActiveToken activeToken;

// возвращает имя сервиса по client_id из входящего https-запроса
const std::string getRequestorFromRequest(const Http::Request& request) {
    std::string client_id = getSQLStringValue("select client_id from tokens where token='"+getAccessToken(request, false)+"'");
    if(client_id.empty()) {
      logcpperror<<"Can't retrieve client_id from saved token"<<ENDL;
    } else {
      std::string ret = std::string(clientIds.getRequestor(client_id));
      logcppdebug<<"Found "<<ret<<" for client_id="<<client_id<<ENDL;
      return ret;
    }
    return "UNKNOWN";
}

// Ресурс: OAuth /token. Обмен кода на токен и обновление токена
class TokenResource : public WResource {
public:
    void handleRequest(const Http::Request& request, Http::Response& response) override {
        //cAuxLog a("TokenResource", request);
        response.setMimeType("application/json");
        const std::string *grant_type = request.getParameter("grant_type");
        const std::string *code = request.getParameter("code");
        const std::string *client_id_ = request.getParameter("client_id");
        std::string client_id = *client_id_;
        std::istream& body = request.in();
        std::string bodyStr(std::istreambuf_iterator<char>(body), {});
        logcppdebug<<bodyStr<<ENDL;
        std::string requestor = clientIds.getRequestor(client_id);
        logcppinfo<<requestor<<" auth request: "<<bodyStr<<ENDL;
        Json::Object result;
        std::string access_token, refresh_token;
        if(*grant_type == "authorization_code") {
          //Если авторизационного запроса еще не было, то и отдавать нечего
          if (codeFirst.empty()) {
              Json::Object error;
              error["error"] = "invalid_grant";
              response.out() << Json::serialize(error);
              logcpperror<<requestor<<". token not issued. Initial code absent"<<ENDL;
              return;
          }
          if (!clientIds.isAuthorized(client_id) || *code != codeFirst) {
              Json::Object error;
              error["error"] = "invalid_auth_request";
              response.out() << Json::serialize(error);
              logcpperror<<requestor<<". token not issued. Not authorized"<<ENDL;
              return;
          }
          // Генерируем access_token
          access_token  = generateToken();
          refresh_token = generateToken();
          activeToken.saveToken(access_token, refresh_token, userFirst, client_id); //сохраняем для дальнейшей работы
          codeFirst.clear();
          userFirst.clear();
          logcppinfo<<requestor<<" token initial generating"<<ENDL;
        } else if(*grant_type == "refresh_token") {
          const std::string *rt = request.getParameter("refresh_token");
          refresh_token = *rt;
          if(refresh_token != activeToken.getRefreshToken(client_id)) {
              Json::Object error;
              error["error"] = "invalid_refresh_request";
              response.out() << Json::serialize(error);
              logcpperror<<requestor<<". Refresh token not matched. Not authorized"<<ENDL;
              return;
          }
          access_token  = generateToken();
          std::string user = activeToken.getUserById(client_id);
          activeToken.saveToken(access_token, refresh_token, user, client_id); //сохраняем для дальнейшей работы.Refresh - не меняется!
          logcppinfo<<requestor<<" token refreshing"<<ENDL;
        } else {
          logcpperror<<requestor<<". Wrong request received (unsupported grant type)"<<ENDL;
        }
          response.setStatus(200);
          // Формируем ответ
          result["access_token"] = access_token.c_str();
          result["token_type"] = "bearer";
          result["expires_in"] = 60*60*23; // 23 часа
          result["refresh_token"] = refresh_token.c_str();
          std::string res = Json::serialize(result).c_str();
          logcppdebug<<requestor<<" token: "<<res<<ENDL;
        response.out() << res;
    }
};


//Возвращает строку между парой символов, например скобок. Используется в парсере
std::string getLimitedString(std::string input, char open, char close) {
   size_t closePos = input.find(close);
   if(closePos == std::string::npos) return "";
   size_t startPos = input.find(open);
   size_t tmpPos = startPos;
   while(1) {
      tmpPos = input.find(open, tmpPos + 1);
      if(tmpPos != std::string::npos) {  //нашли еще одну открывающую скобку, значит закрывающую надо искать дальше
        if(tmpPos < closePos) {  //да, точно есть еще одна открывающая скобка
          size_t clsPos = input.find(close, closePos+1); //ищем следующую закрывашку.
          if(clsPos != std::string::npos) {
            closePos = clsPos;
            tmpPos++;
          } else {
            logcpperror<<"Error parsing Json"<<ENDL;
            return "";
          }
        } else {
          break;
        }
      } else {
        break;
      }
   }
   return input.substr(startPos, closePos-startPos+1);
}

//очищает строку to_clean от символов из строки symbols
void cleanString(std::string& to_clean, std::string symbols) {
    size_t pos; //очищаем от всех лишних символов
    do { pos = to_clean.find_first_of(symbols); if(pos != std::string::npos) to_clean.erase(pos, 1); } while (pos != std::string::npos);
}

// Просто все склеивает в одну строку без пробелов. Удобно в лог, например выводить
std::string toOneLine(std::string& from) {
    std::string ret = from;
    if(ret.length() > 0) { cleanString(ret, " \n\t\r"); }
    return ret;
}

//проверяет равенство количества открывающих и закрывающих скобок.
bool checkParity(std::string& to_check, std::string symbol_open, std::string symbol_close) {
  size_t pos = 0, cnt = 0;
  while(1) { pos = to_check.find(symbol_open, pos); if(pos != std::string::npos) { cnt++; pos++; } else { pos = 0; break; } }
  while(1) { pos = to_check.find(symbol_open, pos); if(pos != std::string::npos) { cnt--; pos++;} else { break; } }
  if(cnt) {
    logcpperror<<"Error checkin string "<<to_check<<": number of '"<<symbol_open<<"' <> '"<<symbol_close<<"'"<<ENDL;
    return false;
  }
  return true;
}

//проверяет, что перед закрывающей скобкой стоит разрешенный символ, а не запятая, например.
bool checkEndSymbols(std::string& to_check) {
  std::string to_check_ = toOneLine(to_check);
  //cleanString(to_check_, " \n\t\r"); //склеиваем все в одну сплошную строку
  if(to_check_.length() < 3) return true;
  size_t pos = to_check_.find_last_not_of("leE0123456789'\"}]{[", to_check_.length()-2);
  if(pos != std::string::npos && pos >= to_check_.length()-2){
     logcpperror<<"Error with last symbol in "<<to_check_<<ENDL;
     return false;
  }
  return true;
}

//преобразовывает строку в нужный тип в зависимости от контента строки
void fillValueFromString(std::string& valueStr, Json::Object& obj, std::string& key) {
            trimString(valueStr);
            // Обрабатываем значение
            if (valueStr == "true") {
                obj[key] = true;
            } else if (valueStr == "false") {
                obj[key] = false;
            } else if (valueStr == "null") {
                obj[key] = Wt::Json::Value();  // null
            } else if (valueStr.size() >= 2 && valueStr[0] == '"' && valueStr.back() == '"') {
                // Строка
                std::string value = valueStr.substr(1, valueStr.size() - 2);
                obj[key] = value.c_str();
            } else {
                // Число (пытаемся преобразовать)
                try {
                    double num = std::stod(valueStr);
                    obj[key] = num;
                } catch (...) {
                    logcppwarn<<"Error parsing json. Unknown value "<<valueStr<<ENDL;
//                    continue;  // Если не число — пропускаем
                }
            }
}

//служебная функция возврата строки, соответствуещей элементу массива из строки, соответствующей массиву
bool getElementOfArray(std::string& s_array, std::string& s_element){
          s_element = getLimitedString(s_array, '{', '}');
          if(!checkParity(s_element, "{", "}")) return false;
          if(!checkEndSymbols(s_element)) return false;
          return true;
}

//МОЙ ПАРСЕР текста в объект Wt::Json. В более поздних версиях Wt имеется свой.
bool parseJsonStringToObject(const std::string& jsonStr, Wt::Json::Object& obj) {
    std::string trimmed = jsonStr;
    // Убираем пробелы в начале/конце
    trimString(trimmed);
    // Проверяем, что это объект: {...}
    if (trimmed.empty() || trimmed[0] != '{' || trimmed.back() != '}') {
    //сюда еще вставить проверку предпоследнего символа
        logcpperror<<"Json has no boundaries: "<<trimmed<<ENDL;
        return false;
    }
    if(!checkEndSymbols(trimmed)) return false;
    // Берём содержимое между { и }
    std::string content = trimmed.substr(1, trimmed.size() - 2);
    cleanString(content, "\n\t\r");
    cleanString(content, "\\");
    size_t start = 0;
    while (start < content.size()) {
        size_t commaPos = content.find(',', start);
        if (commaPos == std::string::npos) {
            commaPos = content.size();
        }
        std::string field = content.substr(start, commaPos - start);
        // Ищем двоеточие, разделяющее ключ и значение
        size_t colonPos = field.find(':');
        if (colonPos == std::string::npos) {
            break;  // Пропускаем некорректное поле: в оригинале было continue, заменено из-за обработки массивов и вложенных объектов
        }
        std::string key = field.substr(0, colonPos);
        trimString(key);
          // Очищаем ключ от кавычек
        if (key.size() >= 2 && key[0] == '"' && key.back() == '"') {
           key = key.substr(1, key.size() - 2);
        }
        size_t arrayPos = field.find('[', colonPos+1);
         // проверяем, что значение не вложенный объект (по-хорошему надо убрать все пробелы и убедиться, что скобка идет сразу за двоеточием.
        size_t bracketPos = field.find('{', colonPos+1);
        bool bArray = false;
        bool bObject = false;
        if( (arrayPos != std::string::npos && bracketPos == std::string::npos) || (arrayPos != std::string::npos && bracketPos > arrayPos) ) bArray = true;
        if( (bracketPos != std::string::npos && arrayPos == std::string::npos) || (bracketPos != std::string::npos && bracketPos < arrayPos) ) bObject = true;
        if(bArray && bObject) { logcpperror<<"This can't happen"<<ENDL; return false; }
        if(bArray) { //если нашли - значит это массив.
          std::string s_rest = content.substr(start+arrayPos); //весь оставшийся Json
          std::string s_array = getLimitedString(s_rest, '[', ']');
          start = start+arrayPos+s_array.length()+1;
          std::string s_element;
          if(!getElementOfArray(s_array, s_element)) return false;
          Json::Array arr;
          while(s_element.length() != 0) {
            s_array = s_array.substr(s_array.find(s_element)+s_element.length());
            Json::Object tmp;
            if(parseJsonStringToObject(s_element, tmp)) arr.push_back(tmp); else return false;
            if(!getElementOfArray(s_array, s_element)) return false;
          }
          obj[key] = arr;
        }
          if(bObject) { //если нашли - значит ищем с конца закрывающую скобку.
            std::string s_rest = content.substr(start+bracketPos/*, content.size()-start-arrayPos*/); //весь оставшийся Json
            std::string s_embedded = getLimitedString(s_rest, '{', '}');
            if(!checkParity(s_embedded, "[", "]")) return false;
            if(!checkEndSymbols(s_embedded)) return false;
            start = start+bracketPos+s_embedded.length()+1;
            Json::Object j_embedded;
            if(parseJsonStringToObject(s_embedded, j_embedded)) obj[key] = j_embedded; else return false;
          } //else {
          if(bArray == false && bObject == false) {
            start = commaPos + 1;
            std::string valueStr = field.substr(colonPos + 1);
            fillValueFromString(valueStr, obj, key);
          }
    }
    return true;
}


//Проверка авторизации запроса от Яндекса
bool isAuthorized(const Http::Request& request, Http::Response& response, std::string& username, bool bNoOut=false) {
        response.setMimeType("application/json");
        std::string access_token = getAccessToken(request, true);
        std::string requestor = getRequestorFromRequest(request);
        logcppdebug<<"Token from "<<requestor<<": "<<access_token<<ENDL;
        try {
          if(!activeToken.isAuthorized(access_token.substr(bearerLength), username) || access_token.substr(0, bearerLength) != "Bearer ") {
            response.setStatus(401);
            if(!bNoOut) {
              Json::Object error;
              error["error"] = "unauthorized";
              response.out() << Json::serialize(error);
            }
            logcpperror<<requestor<<". Wrong token provided. Call from "<<request.clientAddress()<<ENDL;
            return false;
          }
        } catch(...) {
          logcpperror<<"Bad credentials for "<<requestor<<ENDL;
          return false;
        }
        return true;
}

    inline void fillField(Json::Object& to, Json::Object& from, const std::string index) {
       if(from.get(index) != Json::Value::Null) to[index] = from[index];
    }

// Ресурс: /devices (список устройств для Яндекса)
class DevicesResource : public WResource {
private:
public:
    void handleRequest(const Http::Request& request, Http::Response& response) override {
        std::string username;
        if(!isAuthorized(request, response, username)) return;
        std::string request_id   = request.headerValue("X-Request-Id");
        std::string requestor     = getRequestorFromRequest(request);
        logcppinfo<<requestor<<" request for devices descriptions"<<ENDL;
        Json::Array devices; //convert(capability using utf8)
        RowResult res;
        if(device_to_yandex.empty()) {
          res = getDevicesFromDatabase(); //runSQLStatement("select id_for_yandex, convert(device using utf8) as dev from devices");
        } else {
          res = runSQLStatement("select id_for_yandex, convert(device using utf8) as dev from devices where id_for_yandex='"+device_to_yandex+"'");
          device_to_yandex.clear();
        }
        if(res.count()) {
          auto rows = res.fetchAll();
          for(const mysqlx::Row &row : rows ) {
            Json::Object device;
            std::string s_device = std::string(row[1]);
            if(parseJsonStringToObject(s_device, device)) {
              Json::Object retdev;
              retdev["id"] = std::string(row[0]).c_str();
              fillField(retdev, device, "name");
              fillField(retdev, device, "description");
              fillField(retdev, device, "room");
              fillField(retdev, device, "type");
              fillField(retdev, device, "status_info");
              fillField(retdev, device, "device_info");
              fillField(retdev, device, "capabilities");
              fillField(retdev, device, "properties");
              logcppdebug<<"adding device: "<<Json::serialize(retdev)<<ENDL;
              devices.push_back(retdev);
            } else {
              logcpperror<<"Error parsing device: "<<s_device<<ENDL;
            }
          }
        } else {
          logcppwarn<<"List of devices at ymhub is empty"<<ENDL;
        }

        Json::Object result;
        result["request_id"] = request_id.c_str();
        Json::Object payload;
        payload["devices"] = devices;
        payload["user_id"] = username.c_str();  //мы храним 1 токен, привязанный к одному пользователю одного сервиса.
        result["payload"] = payload;
        std::string body = Json::serialize(result);
        logcppdebug<<requestor<<" discovery answer: "<<body<<ENDL;
        response.out() << body;
    }
};

//Обработчик запроса от Яндекса о доступности сервиса
class AvailabilityResource : public WResource {
    void handleRequest(const Http::Request& request, Http::Response& response) override {
      response.setStatus(200);
      response.out()<<"OK";
    }
};

//Обработчик запроса: "отвязка" акаунта. Приходит от Яндекса
class UnlinkResource : public WResource {
    void handleRequest(const Http::Request& request, Http::Response& response) override {
      std::string username;
      if(!isAuthorized(request, response, username)) return;
      response.setStatus(200);
      Json::Object statuses;
      statuses["request_id"] = request.headerValue("X-Request-Id").c_str();
      response.out() << Json::serialize(statuses);
      std::string requestor = getRequestorFromRequest(request);
      logcppwarn<<requestor<<". Account unlinked!!!"<<ENDL;
      std::string access_token = getAccessToken(request, false);
      activeToken.deleteTokenByToken(access_token); //если акаунт отвязан, то токен более не валиден
    }
};


//заполняет статус в ответе для запроса типа action от Яндекса
Json::Object makeActResult(bool bOk) {
  Json::Object ret;
  ret["status"] = bOk?"DONE":"ERROR";
  return ret;
}

//Формирует тело запроса устройству в зависимости от типа запроса
void setBodyText(Wt::Http::Message& mess, Json::Object& api_end, bool bMulti, std::string value) {
   std::string key = "data_"+value;
   std::string body = "";
   if(!bMulti) {  // обычный запрос
     Json::Value _key_ = api_end[key];
     if(_key_.type() == Json::Type::Object) {  //нужно отправить json, а не простое значение
       Json::Object tmp = _key_;
       body = Json::serialize(tmp);
     } else {
       if(_key_ == Json::Value::Null) {      //Если нет ключа "data_XXX", то кладем в body само значение. Оно ВСЕГДА строковое.
         body = value;
       } else {
         body = std::string(api_end[key]);   //А если есть, то находим что отправить в устройство
       }
     }
   } else {       // мультипарт запрос
      std::string p = std::string(api_end[key]);
      std::string v;
      size_t pos = p.find("=");
      if(pos != std::string::npos) {
        v = p.substr(pos+1);
        p = p.substr(0, pos);
        // Генерируем boundary
        std::string boundary = "--" + generateToken().substr(0, 16);
        // Устанавливаем Content-Type с boundary
        mess.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
        // Поле формы (аналог -F "name=value")
        // Поле формы (аналог -F "name=value")
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + p + "\"\r\n\r\n";
        body += v+"\r\n";  // Значение параметра
        body += "--" + boundary + "--\r\n"; // Завершающий boundary
      } else {
        logcpperror<<"Wrong value in API description: "<<p<<ENDL;
      }
  }
  logcppdebug<<"Request to device body: "<<body<<ENDL;
  mess.addBodyText(body);
  body.clear();
}



//Возвращает преобразованное в строку значение из Json. Почему-то я не стал пользоваться библиотечными функциями, так кажется лишние символы лезут.
std::string getStringValueFromJsonValue(Json::Value& v) {
                      int _i_;
                      std::string value = "no_value";
                      switch(v.type()) {
                        case Json::Type::Bool:
                          value = bool(v)?"true":"false";
                          break;
                        case Json::Type::Number:
                          _i_ = v;
                          value = std::to_string(_i_);
                          break;
                        case Json::Type::String:
                          value = std::string(v);
                          break;
                        default:
                          logcpperror<<"Unhandled Json::Value type"<<ENDL;
                          break;
                      }
     return value;
}

//В возвращаемом Яндексу json устанавливает значение ключа, независимо от типа
inline void setSimpleValue(Json::Object& stat, Json::Object&api_resp, std::string key) {
  const std::string value = "value";
  if(api_resp.get(key) != Json::Value::Null) { //если в описании API указан ключ для трансформации значения
    stat[value] = api_resp[key];               //ну т.е. в объекте "response" есть таблица соответствий значений. При этом ключ всегда строковый!
  } else {
    try {
      double num = std::stod(key);
        stat[value] = num;
    } catch (...) {
      stat[value] = key.c_str();      //а если не указан, то просто возвращаем значение
    }
  }
}

//разрешает обработать только то описание API, у которого совпадает значение instance. По идее это может быть только у свойств и не может быть у умений.
//Важно. При запросах типа query, capability берется из описания устройства и содержит объект parameters. При запросах типа action capability берется из
//из самого входящего запроса и там есть объект state и нет parameters
bool isInstance(Json::Object& cap, Json::Object& api) {
  std::string i_params = "parameters";
  std::string i_state = "state";
  std::string i_instance = "instance";
  Json::Object tmp;
  if(cap.get(i_params) != Json::Value::Null) {  //параметры могут быть в описании capability/property
     tmp = cap[i_params];
  } else if(cap.get(i_state) != Json::Value::Null) { //или во входящем запросе типа action
     tmp = cap[i_state];
  }
  if(tmp.get(i_instance) != Json::Value::Null) {  //но в них может и не быть ключа instance. И только если есть проверяем совпадение.
     if(api[i_instance] == tmp[i_instance]) {
        return true;
     }
     return false;
  }
  logcppwarn<<"Neither "<<i_params<<" nor "<<i_state<<" objects were found."<<ENDL;
  return true;
}

// заполняет url в запросе устройству. Выделена отдельно, так как url может быть указан явно, или через трансформационный ключ (что важно для HA).
void fillUrlForDevice(std::string& url, Json::Object& api_end, std::string value) {
  Json::Value _url_ = api_end.get("url");
  if(_url_ != Json::Value::Null) {
    url = std::string(api_end["url"]);
  } else {
    url = std::string(api_end[value]);
  }
}

// выполение внешней программы для обращения к устройству
void runExec(const std::string& execPath, const std::string& params) {
    // Формируем команду: путь к скрипту + аргументы
    std::string command = execPath.substr(7) + " " + params; //убираем exec://
    // Открываем канал для чтения вывода скрипта
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        logcpperror<<"Failed to open pipe for execution: "<<command<<ENDL;
    }
    waitForResponse.forbid();
    // Читаем вывод построчно
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    // Закрываем канал и проверяем код возврата
    int returnCode = pclose(pipe);
    if (returnCode != 0) {
        logcpperror << "Script returned error code: " << returnCode << ENDL;
    }
    logcppinfo<<command<<" returned: "<<result<<ENDL;
    waitForResponse.setDeviceResponse(result);
    waitForResponse.allow();
}

bool isRetrievable(Json::Object& capability) {
  return capability.get("retrievable").orIfNull(true);
}

//Функция, которая делает собственно запросы к устройствам на основании описания API. Используется как в запросах статуса, так и в actions. Форматы ответов Яндексу практически идентичные
bool doRequestToDevice(std::string s_device, std::string s_request, std::string value, Json::Object& device, Json::Object& capability, Json::Object& stat) {
    bool bQuery = (s_request=="query");
    if(bQuery && !isRetrievable(capability)) return false; //на запросах состояния игнорируем те, у которых не извлекается статус.
    Json::Object request = device["request"];
    std::string host = request["host"];
    bool bSubscribed = request.get("subscribe").orIfNull(false); //getBoolParameter(request, "subscribe");
    bool bMQTT = false;
    bool bExec = false;
    bool bHTTP = true;
    std::string capability_type = capability["type"];
    Json::Object tmp = device["api"];
    Json::Array api_array = tmp[capability_type];   //дошли до собственно описания api, соответствующего типу capability/property
    if(host.find("mqtt://") != std::string::npos) { bMQTT = true; bHTTP = false; }
    if(host.find("exec://") != std::string::npos) { bExec = true; bHTTP = false; }
  for(size_t i=0;i<api_array.size();i++) { //это массив, разделяющийся по значению instance
    Json::Object api = api_array[i];
    if(!isInstance(capability, api)) continue;  //Если instance определен и не совпадает с запрошенным - пропускаем.
    //logcppdebug<<"API description: "<<Json::serialize(api)<<ENDL;
    Json::Object api_end = api[s_request]; //дошли до конкретного типа запроса
    Http::Method method;
    std::string s_method = api_end["method"].toString();
    logcppdebug<<"API_END, "<<s_device<<", "<<capability_type<<": "<<Json::serialize(api_end)<<ENDL;
    std::string url;
    Wt::Http::Message mess;
    bool bMulti = api_end.get("multipart").orIfNull(false);//getBoolParameter(api_end, "multipart", false);
    uint32_t timeout = 2;
    if(bMQTT) {  //с устройством будем общаться по mqtt.
      setBodyText(mess, api_end, false, value);
       std::string topic;
       Json::Value _topic_ = request["topic"]; //подписка на общий топик всегда. Статус будет приходить туда
       if(_topic_ != Json::Value::Null) {
          topic = std::string(_topic_);
       } else {
         logcpperror<<"Device "<<s_device<<". In request object key 'topic' not pointed for subscription"<<ENDL;
         return false;
       }
       if((bQuery && bSubscribed) || (s_method.empty())) {  //если действует постоянная подписка, или статус нельзя получить по запросу, то берем статус устройства из базы данных. При этом блок query в описании умения все равно должен быть и method
          getMQTTDeviceState(s_device);
       } else {                                   //е если нет, то ждем получения статуса от устройства.
          auto mc = std::make_unique<cMosquittoClient>(host, topic, timeout);
          mc->request(s_method, mess.body());
       }
    }
    if(bHTTP){     //c устройством будем общаться по сети
      std::transform(s_method.begin(), s_method.end(), s_method.begin(), ::toupper);
      fillUrlForDevice(url, api_end, value);
      if(s_method == "GET") {
         method = Http::Method::Get;
      } else if(s_method == "POST")  {
         method = Http::Method::Post;
         setBodyText(mess, api_end, bMulti, value);
      } else if(s_method == "PUT") {
         method = Http::Method::Put;
         setBodyText(mess, api_end, bMulti, value);
      } else {
        logcpperror<<"Device "<<s_device<<". Wrong (unsupported) method used: "<<s_method<<ENDL;
        return false;
      }
      Json::Object headers = request["headers"];
      std::set<std::string> names = headers.names();
      for(std::string name : names) {
        std::string h_value = headers[name];
        logcppdebug<<"Adding header to request "<<s_method<<": "<<name<<" = "<<h_value<<ENDL;  
        mess.addHeader(name, h_value);
      }
      std::string addr = std::string(request["host"]) + url;
      logcppdebug<<"Device "<<s_device<<". URL formed as: "<<addr<<ENDL;
      auto client = std::make_unique<Http::Client>();
      //------------------------------------------------------------------------------------------------
      client->done().connect([=](boost::system::error_code err, const Wt::Http::Message& response) {
        logcppdebug<<"Device "<<s_device<<". Got to client done()!"<<ENDL;
        if (err) {
            // Обработка сетевой ошибки
            logcpperror << "Device "<<s_device<<". Network error: " << err.message() << ENDL;
        }
        if ((response.status() == 200) || (response.status() == 201)) {
            // Успешный ответ — обрабатываем тело
            std::string resp = response.body();
            if(resp.length() == 0) resp = "_OK_";  //заглушка, на случай если текстовый ответ не придет
            size_t pos = resp.find("\n");
            if(pos != std::string::npos) resp = resp.substr(0, pos);
            waitForResponse.setDeviceResponse(resp);
        } else {
            logcpperror <<"Device "<<s_device<<". HTTP error: " << response.status() << ENDL;
        }
        waitForResponse.allow();
      });
      //------------------------------------------------------------------------------------------------
      client->setTimeout(std::chrono::seconds{timeout});
      waitForResponse.forbid();
      client->request(method, addr, mess);
      addr.clear();
      waitForResponse.wait(timeout);
    }
    if(bExec) {
       setBodyText(mess, api_end, false, value);
       runExec(host, mess.body());
    }
    std::string resp = waitForResponse.getDeviceResponse();
            logcppinfo <<"Device "<<s_device<<". Method: {"<< s_method<< "}, response from the device: " << toOneLine(resp) << ENDL;
            Json::Object api_resp = api["response"];
            if(resp.empty() && bSubscribed) { getMQTTDeviceState(s_device); } //если устройство подписанно на постоянке и не получило ответ, то берем ответ из базы
            if(resp.empty()) {
              logcpperror<<"Device "<<s_device<<": empty response."<<ENDL;
              return false;  //если ответ пустой, то нечего и возвращать
            }
            logcppdebug<<"Device "<<s_device<<". API_RESP: "<<Json::serialize(api_resp)<<ENDL;
            fillField(stat, api, "instance");
            if(bQuery) {
               if(api_resp.get("key") == Json::Value::Null) { //нет ключа для поиска значения в Json. Значит полученный ответ - не Json.
                 setSimpleValue(stat, api_resp, resp);
               } else {  //ответ не plain text, а Json
                 Json::Object _resp_;
                 if(parseJsonStringToObject(resp, _resp_)) {
                   logcppdebug<<"Device "<<s_device<<". Parsed answer from device: "<<Json::serialize(_resp_)<<ENDL;
                   std::string key = api_resp["key"];  //ищем в ответе по ключу, указанному в описании api
                   std::string state = getStringValueFromJsonValue(_resp_[key]);
                   //stat["value"] = api_resp[state];
                   setSimpleValue(stat, api_resp, state);
                 } else {
                   logcpperror<<"Device "<<s_device<<". Error parsing "<<s_request<<" response from device"<<ENDL;
                 }
               }
            } else if(s_request == "action") {
              stat["action_result"] = makeActResult(resp.empty()?false:true);
            } else {
              logcpperror<<"Device "<<s_device<<". Request "<<s_request<<" is not supported"<<ENDL;
              return false;
            }
  }
    return true;
}

//Возвращает json (текст) из базы данных с описанием устройства
std::string getDeviceString(std::string id_for_yandex) {
  return getSQLStringValue("select convert(device using utf8) as dev from devices where id_for_yandex = '"+id_for_yandex+"'");
}

//Возвращает ранее сохраненный статус устройства
std::string getDeviceState(std::string device_id, std::string& requestor, std::time_t *time) {
  RowResult rows = runSQLStatement("select convert(state using utf8), unix_timestamp(updated) from statuses where id_for_yandex='"+device_id+"' and requestor='"+requestor+"'");
  if(rows.count()) {
    Row row = rows.fetchOne();
    *time = std::time_t(row[1]);
    return std::string(row[0]);
  } else {
    logcppwarn<<"No state saved for device "<<device_id<<" and requestor: "<<requestor<<ENDL;
  }
  return "";
}

//сохраняет последний полученный от устройства статус. Всегда сохраняет. Только последний. Нужно для понимания - "спамить" яндекс при обновлении статуса от устроуства или нет
void saveDeviceState(std::string& device_id, Json::Object& dev, std::string& requestor) {
    std::string save_err = "Error saving state of device " + device_id + " and requestor " + requestor + ": ";
    try {
      Session mySession(*mysqlSettings);
      Schema myDB = mySession.getDefaultSchema();
      Table cap  = myDB.getTable("statuses");
      RowResult res = cap.select("state")
                         .where("id_for_yandex=:i_d and requestor=:r_q")
                         .bind("i_d", device_id).bind("r_q", requestor).execute();
      std::string now1  = getDateStringForDB();
      std::string state = Json::serialize(dev);
      if(requestor != idleRequestor) {  //исключаем рекурсивное зацикливание
         std::string idle_state = getSQLStringValue("select state from statuses where id_for_yandex='"+device_id+"' and requestor='"+idleRequestor+"'");
         if(idle_state != state) {           // статус обновился
           saveDeviceState(device_id, dev, idleRequestor);
         }
      }
      if(res.count()) { //будем делать апдейт
         cap.update()
            .set("state", state)
            .set("updated", now1)
            .where("id_for_yandex=:i_d and requestor=:r_q")
            .bind("i_d", device_id).bind("r_q", requestor).execute();
      } else {          //будем сохранять новую запись
         cap.insert("id_for_yandex", "state", "requestor", "updated")
            .values(device_id, state, requestor, now1).execute();
      }
    } catch(const mysqlx::Error &err) {
      logcpperror<<save_err<<err.what()<<ENDL;
    } catch(const std::exception &err) {
      logcpperror<<save_err<<err.what()<<ENDL;
    }

}
//функция заполняет массив devices_resp - это массив сформированных json с ответами на запрос яндекса по одному устройству
void handleRequestToDevice(bool bQuery, std::string& id_for_yandex,Json::Array& devices_resp, Json::Object& req, std::string& requestor) {
             std::string s_device = getDeviceString(id_for_yandex);
             Json::Object device;
             if(parseJsonStringToObject(s_device, device)) {
               logcppdebug<<"Parsed device from database: "<<Json::serialize(device)<<ENDL;
               Json::Object device_resp;
               device_resp["id"] = id_for_yandex.c_str();
               Json::Array targs;
               if(bQuery) {  //это запрос статуса устройства от Яндекса
                 std::set<std::string> blocks { "capabilities", "properties" }; //properties могут только запрашиваться, их не может быть в actions
                 for(std::string block : blocks) {
                   if(device.get(block) != Json::Value::Null) {  //в описании устройства может не быть capabilities или properties, но что-то одно должно быть
                     Json::Array caps = device[block];
                     for (size_t j = 0; j < caps.size(); j++) {
                        Json::Object cap = caps[j];
                        Json::Object stat;
                        logcppdebug<<"Requesting "<<id_for_yandex<<" for "<<std::string(cap["type"])<<ENDL;
                        if(doRequestToDevice(id_for_yandex, "query", "", device, cap, stat)) {
                          //logcppdebug<<Json::serialize(device)<<ENDL;
                          Json::Object targ;
                          fillField(targ, cap, "type");
                          targ["state"] = stat;
                          targs.push_back(targ);
                          logcppdebug<<"Received state for cloud requestor: " << Json::serialize(stat)<<ENDL;
                        } else {
                          //сформировать отчет об ошибке
                          if(isRetrievable(cap)) errors.incError(id_for_yandex);
                        }
                     }
                     device_resp[block] = targs;
                     targs.clear();
                   }
                 }
                 saveDeviceState(id_for_yandex, device_resp, requestor);
               } else { //это запрос действия от Яндекса
                 Json::Array caps = req["capabilities"]; //смотрим все элементы capability во входящем запросе, хотя по идее там должен быть только 1.
                 for (size_t j = 0; j < caps.size(); j++) {
                    Json::Object cap = caps[j];
                    Json::Object stat, _st_;
                    _st_ = cap["state"];
                    Json::Value v = _st_.get("value");
                    if(v != Json::Value::Null) {
                      std::string value = getStringValueFromJsonValue(v);
                      if(doRequestToDevice(id_for_yandex, "action", value, device, cap, stat)) {
                        Json::Object targ;
                        fillField(targ, cap, "type");
                        targ["state"] = stat;
                        targs.push_back(targ);
                        logcppdebug<<"received state for cloud requestor: " << Json::serialize(stat)<<ENDL;
                      } else {
                        // все сообщения в обработчике уже
                        errors.incError(id_for_yandex);
                      }
                    } else {
                      logcpperror<<"Device "<<id_for_yandex<<". There is no value to set in cloud requestor action request."<<ENDL;
                      //errors.incError("YANDEX"); пока не буду использовать. ТОлько ответов от Яндекса, а не для запросов
                    }
                 }
                 device_resp["capabilities"] = targs;
               }
               devices_resp.push_back(device_resp);
             } else {
               logcpperror<<"Error parsing device from database: "<<id_for_yandex<<ENDL;
             }
}


// Статусы и управление устройствами, обработка запроса от Яндекса
class RequestResource : public WResource {
public:
    void handleRequest(const Http::Request& request, Http::Response& response) override {
        std::string username;
        if(!isAuthorized(request, response, username)) return;
        Json::Object statuses;
        Json::Array devices_resp;
        // Читаем тело запроса (JSON)
          std::istream& body = request.in();
          std::string bodyStr(std::istreambuf_iterator<char>(body), {});
          std::string requestor = getRequestorFromRequest(request);
        logcppdebug<<"String from "<<requestor<<": "<<bodyStr<<ENDL;
        logcppinfo<<requestor<<" query/action request received"<<ENDL;
        bool bQuery = (request.path().find("query") != std::string::npos);
        Json::Object act;
        if(parseJsonStringToObject(bodyStr, act)) {
           logcppdebug<<requestor<<".query: "<<Json::serialize(act)<<ENDL;
           Json::Array devices;
           if(bQuery) { devices = act["devices"]; }
           else {
             Json::Object _tmp_ = act["payload"];
             devices = _tmp_["devices"];
           }
           for (size_t i = 0; i < devices.size(); i++) { //запрос может содержать массив устройств. Так работает VK, например.
             Json::Object req = devices[i];
             std::string id_for_yandex = std::string(req["id"]);
             logcppinfo<<requestor<<" "<<(bQuery?"query":"action")<<" request to "<<id_for_yandex<<ENDL;
             handleRequestToDevice(bQuery, id_for_yandex, devices_resp, req, requestor);
           }
        } else {
             logcpperror<<"Error parsing query body: "<<bodyStr<<ENDL;
        }
        statuses["request_id"] = request.headerValue("X-Request-Id").c_str();
        Json::Object tmp;
        tmp["devices"] = devices_resp;
        statuses["payload"] = tmp;
        logcppdebug<<"Responce to "<<requestor<<": "<< Json::serialize(statuses)<<ENDL;
        response.out() << Json::serialize(statuses);
    }
};

#ifdef __DEBUG__
//обработчик SIGFAULT
void mySIGFAULThandler(int sig) {
  void *array[20];
  size_t size;
  // get void*'s for all entries on the stack
  size = backtrace(array, 20);
  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}
#endif

//Функция отправки запроса в сервис уведомлений Яндекс (или иного сервиса, если настроено).
cWaitForResponse waitReport; //используется для ожидания ответа от яндекса. Превращает асинхронный обмен в условно синхронный
void doReportRequest(Http::Client *client, Json::Object& statuses, std::string& url, std::string token) {
    Wt::Http::Message mess;
    mess.addHeader("Authorization", "OAuth "+token);
    mess.addHeader("Content-Type", "application/json");
    std::string body = Json::serialize(statuses);
    logcppdebug<<"Reporting body to external service: "<<body<<ENDL;
    mess.addBodyText(body);
    logcppdebug<<"url = "<<url<<ENDL;
    uint32_t timeout = 2;
    client->setTimeout(std::chrono::seconds{timeout});
    waitReport.forbid();
    client->request(Http::Method::Post, url, mess);
    waitReport.wait(timeout);
}

// Функция определяет живо ли устройство. Но только для тех, у кого установлен параметр maxidletime
bool checkDeviceAlive(std::string& device_id) {
  if(!device_id.empty()) {
    std::string s_device = getDeviceString(device_id);
    if(!s_device.empty()) {
      Json::Object device;
      if(parseJsonStringToObject(s_device, device)) {
        double maxidletime = device.get("maxidletime").orIfNull(0);  //в секундах
        if(maxidletime == 0) return true;                            //не контролируем простой устройства.
        std::time_t time;
        std::string old_state = getDeviceState(device_id, idleRequestor, &time); //получаем самое (!) старое значение статуса устройства. Вернее только время нас интересует
        std::time_t currentTime = std::time(nullptr);
        double delta = difftime(currentTime, time);
        if(delta > maxidletime) {
          logcppwarn<<"Device "<<device_id<<" is dead. Last state was updated "<<timeToRussianFormat(&time)<<ENDL; //" and state is: "<<old_state
          return false;                                              //простой устройства
        }
      } else {
        logcpperror<<"Error parsing device "<<device_id<<ENDL;
      }
    } else {
      logcpperror<<"Device "<<device_id<<"not found"<<ENDL;
    }
  } else {
    logcpperror<<"device_id is empty"<<ENDL;
  }  
  return true;                                                        // во всех остальных случаях живое устройство
}

/* функция отправки статуса устройства в умный дом Яндекса или иного внешнего сервиса.
Использует тот же механизм вызовов устройств, что и по обычным запросам от Яндекса: handleRequestToDevice
При этом всегда опрашивается устройство целиком, а не по отдельным properties/capabilities.
Чтобы не спамить яндекс, а также следуя документации, где сказано, что reportable устройство отправляет статусы по факту изменения
мы будем хранить строку состояния устройства (по факту это json), которую сформирует handleRequestToDevice. Собственно handleRequestToDevice и сохраняет последний статус
по каждому устройству, независимо от того, было обращение от Яндекс или от reportDeviceStatus
Здесь же мы просто сравним строки, если они отличаются тогда отправляем новый статус в Яндекс
Не очень красиво, зато дешево и надежно.*/
void reportDeviceStatusToOneService(std::string& device_id, std::string& client_id, externalService *es) {
   if(es->out_url_state.empty()) return; //и если в нем указан url, куда слать статус
   Json::Array devices_resp; //в реальности функция отвечает всегда только 1м устройством в массиве
   Json::Object req;         //это заглушка, в запросах на чтение не используется
   std::time_t time;
   std::string requestor = es->requestor;//clientIds.getRequestor(client_id);  //service->requestor;//clientIds.getYandexName();
   std::string old_state = getDeviceState(device_id, requestor, &time); //получаем старое значение статуса устройства.
   handleRequestToDevice(true, device_id, devices_resp, req, requestor);  //вот здесь получаем статусы от устройства и кладем их в devices_resp. Поскольку у нас одно устройство, то и в json'е будет только одно устройство
   if(devices_resp.size()) {
     Json::Object device_resp = devices_resp[0]; // там всегде одно устройство только
     std::string new_state = Json::serialize(device_resp);
     if(new_state == old_state) { //старый и новый статусы одинаковы
       //если устройство активно (true), то можем отправить статус. 
       //если умерло, то не отправляем статус в сервис, чтобы в нем можно было настроить сценарии уведомления о неактивности устройства
       if(checkDeviceAlive(device_id) == true) { 
         std::time_t currentTime = std::time(nullptr);
         double delta = difftime(currentTime, time);
         if(delta < secondsUnchanged) {  // и если разница во времени меньше настроенной в параметрах величины, то не отправляем повторно
           logcppinfo<<"State of device "<<device_id<<" unchanged "<<delta<<"s. Less than allowed "<<secondsUnchanged<<"s. Skipping report for "<<requestor<<ENDL;
           return;
         }
       }
     }
   } else {
     logcppwarn<<"Device "<<device_id<<" returned empty status."<<ENDL;
     return;
   }
   Json::Object statuses;
        statuses["ts"] = std::time(nullptr); //Время возникновения события в секундах, формат unix timestamp.
        Json::Object tmp;
        tmp["user_id"] = activeToken.getUserById(client_id).c_str();
        tmp["devices"] = devices_resp;
        statuses["payload"] = tmp;
    Http::Client *client = new (Http::Client);
    //------------------------------------------------------------------------------------------------
    client->done().connect([=](boost::system::error_code err, const Wt::Http::Message& response) {
        logcppdebug<<"Got to client done()!"<<ENDL;
        if (err) {
            // Обработка сетевой ошибки
            logcpperror << "Network error: " << err.message() << ENDL;
        } else if (response.status() == 202) {
            // Успешный ответ — обрабатываем тело
            logcppinfo<<"Status of "<<device_id<<" accepted by "<<requestor<<ENDL;
        } else {
            Json::Object resp;
            parseJsonStringToObject(response.body(), resp);
            logcpperror << requestor<<" returned: " << std::string(resp["error_message"]) << ENDL;
            errors.incError(requestor);
       }
        waitReport.allow();
    });
    //------------------------------------------------------------------------------------------------
    if(client) {
      logcppinfo<<"Reporting "<<device_id<<" status to "<<requestor<<ENDL;
      doReportRequest(client, statuses, es->out_url_state, es->out_token);
      delete client;
    }
}
// основной вызов: пробегает по всем сервисам, и у кого настроена возможность репортинга вызывает уже функцию отправки статуса
void reportDeviceStatus(std::string& device_id) {
  clientIds.rewind();
  std::string client_id;
  externalService *es = clientIds.getNext(client_id);
  while(es) {
    reportDeviceStatusToOneService(device_id, client_id, es);
    es = clientIds.getNext(client_id);
  }
}


// получение device_id из запроса типа "сигнал".
bool getIdFromSignalAndRequest(Json::Object& sig) {
  std::string id_for_yandex = sig.get("id").orIfNull("");
  if(!id_for_yandex.empty()) {
      logcppinfo<<"Requesting device "<<id_for_yandex<<" by signal from device"<<ENDL;
      reportDeviceStatus(id_for_yandex);
  } else { return false; }
  return true;
}

// Ресурс: получение сигнала от устройства, что его статус обновился. Сам статус не передается. Его надо запрашивать отдельно. Конечно, лучше бы передавался статус,
// но тогда надо сильно перекраивать логику отправки данных в Яндекс. Замечу, что сам Яндекс, когда запрашивает устройство, он запрашивает сразу все, а не отдельный capability/property
// Устройство (или умный дом) посылает простейший JSON в формате { "id": device_id }
class SignalResource : public WResource {
private:
public:
    void handleRequest(const Http::Request& request, Http::Response& response) override {
        response.setMimeType("application/json");
        std::string access_token = getAccessToken(request, true);
        logcppdebug<<"Token from device: "<<access_token<<ENDL;
        externalService *es = clientIds.isAuthorizedByToken(access_token);
        if(es == nullptr) {
            response.setStatus(401);
            Json::Object error;
            error["error"] = "unauthorized";
            response.out() << Json::serialize(error);
            logcpperror<<"Some device. Wrong token provided. Call from "<<request.clientAddress()<<ENDL;
            return;
        }
        // Читаем тело запроса (JSON)
        std::istream& body = request.in();
        std::string bodyStr(std::istreambuf_iterator<char>(body), {});
        logcppdebug<<"Signal from device: "<<bodyStr<<ENDL;
        logcppinfo<<"Signal request received from "<<es->requestor.c_str()<<ENDL;
        Json::Object sig;
        response.setStatus(200);
        if(parseJsonStringToObject(bodyStr, sig)) {
          if(getIdFromSignalAndRequest(sig)) {
          } else {
            try {
              const Json::Array& devices = sig.get("devices");
              for (size_t j = 0; j < devices.size(); j++) {
                Json::Object device = devices[j];
                if(!getIdFromSignalAndRequest(device)) {
                  logcpperror<<"Device provided wrong id for cloud requestor"<<ENDL;
                  response.setStatus(400);
                }
              }
            } catch (...) {
              logcpperror<<"Device didn't provide id for cloud requestor"<<ENDL;
              response.setStatus(400);
            }
          }
        } else {
          logcpperror<<"Wrong json provided from device"<<ENDL;
          response.setStatus(400);
        }
//        response.out()<<"Ok";
    }
};


//Здесь хардкодится обработка шаблонов ответа из таблицы sb_codes.
//В общем случае используется подстановка определенных фраз вместо зарезервированых ключевых слов, начинающихся с $
std::string getAnswerString(Json::Object& d_state, Json::Object& state) {
  std::string i_instance = "instance";
  std::string i_value = "value";
  std::string i_response = "response";
  std::string ret = "";
  if(d_state[i_response].type() != Json::Type::Null) { //массив, сложный ответ, например о погода с темп и влаж
    Json::Array d_states = d_state[i_response];
    for(gsize i=0;i<d_states.size();i++) {
      ret += getAnswerString(d_states[i], state);
    }
    return ret;
  }
  if(d_state[i_instance] == state[i_instance]) {  //совпало описание с полученным статусом
     std::string method = d_state[i_value];
     if(state[i_value].type() == Wt::Json::Type::Bool) { //это все on_off capabilities. Для них просто возвращаем слово в зависимости от типа устройства
        bool val = state[i_value];
        if(method == "$on_off1") {
          ret = val?"включен":"выключен";
        } else if(method == "$on_off2") {
          ret = val?"включена":"выключена";
        } else if(method == "$on_off3") {
          ret = val?"открыта":"закрыта";
        }
     } else if(state[i_value].type() == Wt::Json::Type::Number) {
        double value = state[i_value];
        std::string to_find = "$celsius";
        size_t value_pos = method.find(to_find);
        if(value_pos != std::string::npos) {
          method.replace(value_pos, to_find.length(), NumberToWords::convert(value, UnitType::DEGREE));  //а здесь заменяем ключевое слово, так как может быть фраза с ним
        } else {
          to_find = "$percent";
          value_pos = method.find(to_find);
          if(value_pos != std::string::npos) {
            method.replace(value_pos, to_find.length(), NumberToWords::convert(value, UnitType::PERCENT));
          }
        }
        ret = method;
     }
     ret += ". ";
  }
  return ret;
}

// вспомогательная структура данных для работы с голосовыми помощниками
struct vaData {
  std::string requestor;
  std::string s_response;
  int ret_code;
};

//Унифицированная для голосовых помощников функция обработки запроса по формату, разработанному для Салюта.
void handleRequestFromVoiceAssistant(std::string bodyStr, vaData& va){
        Json::Object voice;
        va.ret_code = 200;
        if(parseJsonStringToObject(bodyStr, voice)) { //парсим полученный от помощника запрос
          std::string room, entity;
          room = std::string(voice["room"]);
          to_lower_russian(room);
          entity = std::string(voice["entity"]);
          to_lower_russian(entity);
          std::string request = std::string(voice["request"]);
          //Получаем запись с описанием, что делать для этого entity и этой room
          RowResult res = runSQLStatement("select room, entity, ids_for_yandex, (select statement from sb_codes where state=sb_requests.action) as act, (select statement from sb_codes where state=sb_requests.state) as state from sb_requests where room='"+room+"' and entity='"+entity+"'");
          if(res.count()) {
            Row row = res.fetchOne();  //может быть только одна строка, это регулируется индексом в БД
            size_t start = 0;
            std::string ids = std::string(row[2]);
            size_t commaPos;
            do {   //Устройств, соответствующих команде может быть несколько (Например Свет 1 и Свет 2). Разделены запятой.
               std::string id_for_yandex;
               commaPos = ids.find(',', start);
               if(commaPos == std::string::npos) { id_for_yandex = ids.substr(start); }
               else                              { id_for_yandex = ids.substr(start, commaPos); start = commaPos + 1; }
               trimString(id_for_yandex);
               Json::Object req;
               Json::Array devices_resp;  //если вынести за цикл, то здесь будет массив ответов от устройств, а так каждый раз только 1 значение в массиве
               bool bQuery = request=="query"?true:false;
               if(bQuery) {  //запрос статуса
                 handleRequestToDevice(bQuery, id_for_yandex, devices_resp, req, va.requestor);
                 // после обработки запроса проверяем, что устройство живо. 
                 if(checkDeviceAlive(id_for_yandex) == false) {
                    try {
                      Json::Object device;
                      parseJsonStringToObject(getDeviceString(id_for_yandex), device);
                      va.s_response = device.get("description").orIfNull("Устройство ") + " не отвечает более чем " + NumberToWords::convert(device.get("maxidletime").orIfNull(0), UnitType::NONE) + " секунд";
                    } catch(...) {
                      va.s_response = "Устройство давно не отвечает";
                    }
                    return;   //выходим из функции, ответ уже есть
                 }
                 Json::Object resp = devices_resp[0];  //пока массив внутри цикла, в нем всегда 1 элемент
                 logcppdebug<<id_for_yandex<<" query response: "<<Json::serialize(resp)<<ENDL;
                 std::string s_state = std::string(row[4]); //текстовое описание как показывать статус
                 Json::Object d_state;
                 if(parseJsonStringToObject(s_state, d_state)) {
                   std::set<std::string> blocks { "capabilities", "properties" }; //properties могут только запрашиваться, их не может быть в actions
                   for(std::string block : blocks) {
                     if(resp[block].type() != Json::Type::Null) {
                       Json::Array caps = resp[block];
                       for(unsigned int i=0;i<caps.size();i++) {
                         Json::Object cap   = caps[i];
                         Json::Object state = cap["state"];
                         va.s_response += getAnswerString(d_state, state);
                       }
                     }
                   }
                 } else {
                   logcpperror<<id_for_yandex<<": can't parse description for state: "<<s_state<<ENDL;
                 }
               } else {  //команда на исполнение
                 std::string s_cap = std::string(row[3]);  //текстовое описание capability в формате Яндекс
                 std::string to_find = "\"$value\"";     //в нем нужно заменить ключевое слово на полученное от Салюта
                 size_t value_pos = s_cap.find(to_find);
                 if(value_pos != std::string::npos) {
                   s_cap.replace(value_pos, to_find.length(), getStringValueFromJsonValue(voice["value"]));  //заменяем
                   logcppdebug<<id_for_yandex<<" cap: "<<s_cap<<ENDL;
                   if(parseJsonStringToObject(s_cap, req)) {
                     logcppdebug<<id_for_yandex<<" json: "<<Json::serialize(req)<<ENDL;
                     handleRequestToDevice(bQuery, id_for_yandex, devices_resp, req, va.requestor);  //по хорошему надо проверять что в ответах.
                     logcppdebug<<Json::serialize(devices_resp)<<ENDL;
                     Json::Object resp = devices_resp[0];
                     logcppdebug<<Json::serialize(resp)<<ENDL;
                     Json::Array caps = resp["capabilities"];
                     for(unsigned int i=0;i<caps.size();i++) {
                       Json::Object cap = caps[i];
                       logcppdebug<<Json::serialize(cap)<<ENDL;
                       Json::Object state = cap["state"];
                       logcppdebug<<Json::serialize(state)<<ENDL;
                       Json::Object actr = state["action_result"];
                       logcppdebug<<Json::serialize(actr)<<ENDL;
                       if(std::string(actr["status"]) == "DONE") {
                         va.s_response += "выполнено. ";
                       } else {
                         va.s_response += "что-то пошло не так.";
                         va.ret_code = 421;
                       }
                     }
                   } else {
                     logcpperror<<va.requestor<<" action can't be parsed for device "<<id_for_yandex<<ENDL;
                   }
                 } else {
                   logcpperror<<va.requestor<<" action doesn't contain $value for replacement. Device "<<id_for_yandex<<ENDL;
                 }
               }
            } while (commaPos != std::string::npos);
          } else {
            logcpperror<<va.requestor<<". No settings found for room "<<room<<" and entity "<<entity<<ENDL;
            va.s_response = "не найдено устройство";
            va.ret_code = 421;
          }
        } else {
          logcpperror<<"Wrong json provided from "<<va.requestor<<ENDL;
          va.s_response = "Плохой Json у " + va.requestor;
          va.ret_code = 400;
        }
}

//Возвращает вложенный Json объект произвольной глубины вложенности. Просто перечисляются уровни последовательно в аргументах функции.
template<typename T, typename... Args> Json::Object getNestedObject(T&& source, Args&&... args) {
    // Создаём массив из аргументов
  Json::Object start;
  try {
    std::array<std::string, sizeof...(args)> strings = {
        std::forward<Args>(args)...
    };
    // Теперь можно обращаться по индексу
    start = Json::Object(source)[strings[0]];
    for (size_t i = 1; i < strings.size(); i++) {
       logcppdebug<<"Getting '"<<strings[i]<<"' object from '"<<strings[i-1]<<"'"<<ENDL;
 //      start = start[strings[i]];  //вот такая простая конструкция не работает в некоторых случаях. Почему - не разобрался.
       Json::Value v = start.get(strings[i]);
       if(v != Json::Value::Null) {
         start = Json::Object(v);
       } else {
         logcppwarn<<"Object "<<strings[i]<<" not found!"<<ENDL;
         break;
       }
    }
  } catch(...) {
     logcpperror<<"Error getting nested object"<<ENDL;
  }
  logcppdebug<<Json::serialize(start)<<ENDL;
  return start;
}
//Возвращает значение ключа value из объекта по имени key
std::string getValueFromJson(Json::Object& obj, std::string key) {
  try {
    Json::Object tmp = obj[key];
    return tmp["value"];
  } catch(...) {
    std::string s = Json::serialize(obj);
    logcppwarn<<"Couldn't get "<<key<<" from json: "<<toOneLine(s)<<ENDL;
    return "";
  }
}
//Возвращает синоним слова, чтобы не менять логику обработки запросов от голосовых помощников. Логика остается настроенной единожды для всех помощников.
std::string getSynonym(std::string& src) {
  return getSQLStringValue("select entity_dst from sb_synonyms where entity_src='"+src+"'");
}

//Запросы от общего навыка Алисы (не умный дом). Делается приведение полученного запроса к тому же формату, что и у Салюта, далее логика одинаковая.
/**************************************************************************************************************************************************
1. В обычных навыках access_token не передается в Headers. Он передается в теле запроса.
2. Токен не передается в случае, если запрос приходит с другого акаунта, которому в натройках навыка предоставлен доступ. Передается только user_id
в теле запроса. Это можно исправить, если сделать петлю авторизации через сервер OAuth самого Яндекса. При этом токен начинает передаваться в Headers
Но, в этом случае, придется каждого пользователя привязывать к бэкенду и генерировать токен. Поскольку client_id одинаковый, это приводит к сбросу
токена для основного акаунта. Это можно вылечить, если токены хранить не по client_id, а по user_id. Однако, user_id можно получить только после
генерации токена и сохранения его в Яндексе, что полностью ломает логику хранения токенов. И не решает проблему обновления токенов.
3. Проще настроить еще один приватный навык с идентичными параметрами на акаунте Яндекс, которому дается доступ.
4. Или сделать двойной тип авторизации. Либо по токену для основного акаунта либо по user_id для того, кому предоставлен доступ. Для этого придется
как минимум один раз разобрать тело запроса, чтобы вытащить user_id.
Итоговое решение: не использовать связку акаунтов, а использовать для авторизации user_id. Для этого придется вытаскивать user_id из логов.
**************************************************************************************************************************************************/
class AliceResource : public WResource {
private:
public:
    void handleRequest(const Http::Request& request, Http::Response& response) override {
     //cAuxLog a("AliceResource", request);
        std::string s_resp;//, username;
        std::istream& body = request.in();
        std::string bodyStr(std::istreambuf_iterator<char>(body), {});
        Json::Object alice;
        vaData va;
        Json::Object answer, resp;
        if(parseJsonStringToObject(bodyStr, alice)) {
          //Итоговое решение - авторизация по user_id, которые храним в конф-файле в особом формате
          if(clientIds.isAuthorizedByUserId(getNestedObject(alice,"session","user").get("user_id").orIfNull(""), va.requestor)) {
            logcppdebug<<"Alice authorized by token from request body"<<ENDL;
            //дальше просто продолжаем работу
/*          } else { //этот блок для авторизации по токену в Headers по аналогии с умным домом Яндекс.
            if(!isAuthorized(request, response, username, true)) {
               logcppdebug<<"Requesting Alice to get token"<<ENDL;
               Json::Object tmp;
               parseJsonStringToObject("{ \"response\": { \"text\": \"Необходима авторизация\", \"tts\": \"Необходима авторизация\", \"end_session\": false, \"directives\": { \"start_account_linking\": {} } }, \"version\": \"1.0\" }", tmp);
               logcppdebug<<Json::serialize(tmp)<<ENDL;
               response.out()<<Json::serialize(tmp)<<ENDL;
               response.setStatus(200);
               return;
            }
            va.requestor = getRequestorFromRequest(request);
            }*/
            resp["end_session"] = false;
            logcppdebug<<va.requestor<<" request: "<<bodyStr<<ENDL;
            logcppinfo<<va.requestor<<". Request received."<<ENDL;
            Json::Object slots = getNestedObject(alice, "request", "nlu", "intents", "smarthouse", "slots");
            std::string entity = getValueFromJson(slots, "entity");
            if(!entity.empty()) {  //У Алисы - английские слова. У Салюта русские. Преобразуем англ к русс с помощью словаря синонимов. Иначе пришлось бы описывать таблицу соответствия слов идентификаторам устройст повторно для Алисы. А так сохраняется настройка логики в одном месте
              std::string room   = getValueFromJson(slots, "room");
              std::string action = getValueFromJson(slots, "value");
              logcppdebug<<"entity: "<<entity<<", room: "<<room<<", action: "<<action<<ENDL;
              std::string body = "{ \"room\": \"" + getSynonym(room) + "\", \"entity\": \"" + getSynonym(entity) + "\", \"request\": \"" + ((action=="other")?"query":"action") + "\", \"value\": \""+action+"\" }";
              handleRequestFromVoiceAssistant(body, va);
              resp["end_session"] = true;  //если отработал корректно, прекращаем диалог
            } else {
              if(getNestedObject(alice,"session").get("new").orIfNull(false) == true) {  //если новая сессия, то надо просто приветствие произнести
                va.s_response = "Командуйте, хозяин!";
              } else {
                logcpperror<<"Intent absent in "<<va.requestor<<" request"<<ENDL;
                va.s_response = "Не настроен интент в навыке Алисы";
              }
            }
          } else {
             logcpperror<<"Non authorized request from Alice, no user_id found"<<ENDL;
             va.s_response = "Пользователь не авторизован (user_id не настроен в навыке)";
          }

        } else {
          va.s_response = "Не могу распознать запрос от хозяина";
        }
        resp["text"]        = va.s_response.c_str();
        answer["response"]  = resp;
        answer["version"]   = "1.0";
        s_resp = Json::serialize(answer);
        logcppdebug<<s_resp<<ENDL;
        response.setStatus(200/*va.ret_code*/); //в отличие от Салюта здесь диалого не прерывается и я должен вернуть, что случилось.
        response.out()<<s_resp;
    }
};

//Ресурс: получение запроса от Сбер.Салют.
//Формат запроса от Салюта: { "room": "xxx", "entity": "yyy", "request": "action/query", "value": value } //value передается как есть, но можно и в кавычках
//Далее вызывается handleRequestToDevice из handleRequestFromVoiceAssistant, имитируя таким образом запросы от Яндекса, но без передачи ответов Яндексу.
//Аналогичный подход используется при запросе статуса устройства по таймеру.
class SaluteResource : public WResource {
private:
public:
    void handleRequest(const Http::Request& request, Http::Response& response) override {
        response.setMimeType("application/json");
        std::string access_token = getAccessToken(request, true);//request.headerValue("Authorization");
        logcppdebug<<"Token from voice assistant: "<<access_token<<ENDL;
        externalService *es = clientIds.isAuthorizedByToken(access_token);
        if (es == nullptr) {
            response.setStatus(401);
            Json::Object error;
            error["error"] = "ошибка авторизации";
            response.out() << Json::serialize(error);
            logcpperror<<"External voice assistant provided wrong token."<<ENDL;
            return;
        }
        // Читаем тело запроса (JSON)
        std::istream& body = request.in();
        std::string bodyStr(std::istreambuf_iterator<char>(body), {});
        logcppdebug<<es->requestor.c_str()<<" request: "<<bodyStr<<ENDL;
        logcppinfo<<es->requestor.c_str()<<". Request received."<<ENDL;
        vaData va;
        va.requestor = es->requestor;
        handleRequestFromVoiceAssistant(bodyStr, va);
        response.setStatus(va.ret_code);
        response.out()<<va.s_response;
    }
};


//Функция будет отправлять описания устройств облачному сервису при его изменении. Вернее командует сервису, что нужно заново запросить ВСЕ описания устройств
void requestDevicesChangeToOneService(std::string& client_id, externalService *es) {
    if(es->out_url_discovery.empty()) return;  //работаем только если есть url куда слать изменение
    Http::Client *client = new (Http::Client);
    //------------------------------------------------------------------------------------------------
    client->done().connect([=](boost::system::error_code err, const Wt::Http::Message& response) {
        logcppdebug<<"Got to client done()!"<<ENDL;
        if (err) {
            // Обработка сетевой ошибки
            logcpperror << "Network error: " << err.message() << ENDL;
        } else if (response.status() == 202) {
            // Успешный ответ — обрабатываем тело
            logcppinfo<<"Request for devices discovery accepted by "<<es->requestor.c_str()<<ENDL;
        } else {
            Json::Object resp;
            parseJsonStringToObject(response.body(), resp);
            logcpperror << es->requestor<<" returned: " << std::string(resp["error_message"]) << ENDL;
            errors.incError(es->requestor);
        }
        waitReport.allow();
    });
    //------------------------------------------------------------------------------------------------
    logcppinfo<<"Reporting "<<es->requestor.c_str()<<" that device(s) have been changed "<<device_to_yandex<<ENDL;
    Json::Object statuses;
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() / 1000.0;
    statuses["ts"] = Json::Value(timestamp);
    Json::Object tmp;
    tmp["user_id"] = activeToken.getUserById(client_id).c_str();
    statuses["payload"] = tmp;
    if(client) {
      doReportRequest(client, statuses, es->out_url_discovery, es->out_token);
      delete client;
    }
}
//проходит по всем внешним сервисам.
void requestDevicesChange() {
  clientIds.rewind();
  std::string client_id;
  externalService *es = clientIds.getNext(client_id);
  while(es) {
    requestDevicesChangeToOneService(client_id, es);
    es = clientIds.getNext(client_id);
  }
}

// класс организует хранение контекстов таймеров. В контексте лежит device_id как параметр таймера.
// выделяем память для каждого device_id и храним указатели в векторе.
// при удалении таймеров высвобождаем память и чистим вектор
class cReportingTimers {
  private:
    std::vector<char *> devices_;  //здесь будем хранить адреса выделенных блоков памяти. Для того, чтобы иметь возможность их освободить
  public:
    void addTimer(unsigned long interval, std::string& device_id) {
       char *d = (char*)malloc(device_id.length()+1);
       if(d) {
         strcpy(d, device_id.c_str());
         devices_.push_back(d);
         new cTimer(interval, &cReportingTimers::doReport, true, (void*)d);
       } else {
         logcpperror<<"Error allocating memory for timer context"<<ENDL;
       }
    }
    static void doReport() {
      std::string device_id = (std::string)(char*)(timer_currentTimer->getContext());
      logcppinfo<<"Requesting device "<<device_id<<" by timer"<<ENDL;
      reportDeviceStatus(device_id);
    }
    void cleanAll() {
       for(char* d : devices_) {
         free(d);
       }
       devices_.clear();
    }
};
cReportingTimers reportingTimers;

//функция пробегает по устройствам и там, где установлен interval, запускает таймер на обновление статуса.
//все таймеры удаляются и создаются снова.
void setDeviceReporting() {
  //сначала проверки на уровне БД
  if(!mysqlSettings) return;
  RowResult rows = getDevicesFromDatabase();
  try {
    if(rows.count() == 0) {
      logcppwarn<<"List of devices empty. Can't set reporting timers"<<ENDL;
      return;
    }
  } catch(...) { return; }
  main_mtx.lock(); //нужно остановить основной цикл на время обновления таймеров и подписок
  reportingTimers.cleanAll();  //до удаления таймеров нужно освободить память занятую их членами context
  timer_killall();
  subscriptions.cleanSubscriptions();
  for(Row row : rows) {
    Json::Object device;
    if(parseJsonStringToObject(std::string(row[1]), device)) {
      std::string device_id = std::string(row[0]);
      Json::Value v = device.get("interval");
      if(v != Json::Value::Null) {
        long interval = long(v) * 1000;
        logcppinfo<<"Creating reporting timer for "<<device_id<<" with "<<interval<<"ms"<<ENDL;
        reportingTimers.addTimer(interval, device_id);
      }
      std::string name = "request";
      if(device.contains(name)) {
         Json::Object request = device[name];
         if(request.get("subscribe").orIfNull(false)) { //подписываемся на получение статусов устройства
           std::string topic; name = "topic";
           if(request.get(name) != Json::Value::Null) {
             topic = std::string(request.get(name));
             subscriptions.newSubscription(device_id, request["host"], topic);  //хорошо бы еще host проверять.
           } else {
             logcpperror<<"'topic' key not defined in 'request' object for device "<<device_id<<ENDL; 
             errors.incError(device_id);
           }
         }
      } else {
        std::string device_id = std::string(row[0]);
        logcpperror<<"Device "<<device_id<<" doesn't contain object 'request'"<<ENDL;
        errors.incError(device_id);
      }
    }
  }
  main_mtx.unlock(); //разрешаем продолжение работы
  main_cv.notify_one();
}

//код функции взят (убран throw) отсюда https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ return std::string(""); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}
//Отправляет после полуночи статистику по работе за сутки
void reportStatistics() {
  time_t rawtime;
  struct tm *timeinfo;
  static int day = 0;
  time( &rawtime );                               // получить текущую дату, выраженную в секундах
  timeinfo = localtime( &rawtime );
  if(day != timeinfo->tm_mday) {
    if(day) {
      double vm_usage, resident_set;
      auto sessions = Wt::WServer::instance()->sessions();
      calcMemorySize(vm_usage, resident_set);
      std::string report = "VM: "+string_format("%.0f", vm_usage)+"Kb, RSS: "+string_format("%.0f", resident_set)+"Kb, Sessions: "+std::to_string(sessions.size());
      report += "\n\n";
      report += errors.getReport();
      logcppinfo<<report<<ENDL;
      sendMail(mailto, "ya_hub report", report);
      errors.cleanUp();
    }
    day = timeinfo->tm_mday;
  }
}


int main(int argc, char *argv[]) {
    char *config_file = nullptr;
#ifdef __DEBUG__
    signal(SIGSEGV, mySIGFAULThandler);   // install trace handler
#endif
    //сначала читаем только путь к конфигурационному файлу
    for(int acount=1;acount<argc;acount++) {
       if(strcmp(argv[acount], "-c")==0) {
         if(acount < argc-1) {
           acount++;
           config_file = argv[acount];
           //acount = argc;
           break;
         }
       }
    }
    //читаем конфигурационный файл
    cWServerParameters *sparams = new cWServerParameters;
    readValues(config_file, sparams);
    logcppinfo<<"ya_hub started. "<<getVersion()<<ENDL;

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    manageArgv(argc, argv);
    //Веб сервер можно запустить только из main(). Не разобрался почему так.
    WServer server("");
    try {
      server.setCustomLogger(wlogger);
      std::vector<std::string> _argv_ = sparams->getParams();
      server.setServerConfiguration(argv[0], _argv_, WTHTTP_CONFIGURATION);
        // Регистрируем ресурсы
        server.addResource(std::make_shared<TokenResource>(), "/oauth/token");
        server.addResource(std::make_shared<AvailabilityResource>(), "/v1.0");
        server.addResource(std::make_shared<DevicesResource>(), "/v1.0/user/devices");
        server.addResource(std::make_shared<RequestResource>(), "/v1.0/user/devices/query");
        server.addResource(std::make_shared<RequestResource>(), "/v1.0/user/devices/action");
        server.addResource(std::make_shared<UnlinkResource>(),  "/v1.0/user/unlink");
        server.addResource(std::make_shared<SignalResource>(),  "/v1.0/signal");
        server.addResource(std::make_shared<SaluteResource>(),  "/v1.0/salute");
        server.addResource(std::make_shared<AliceResource>(),   "/v1.0/alice");
      // add a single entry point, at the default location (as determined
      // by the server configuration's deploy-path)
      server.addEntryPoint(EntryPointType::Application, [](const WEnvironment &env) {
      /*
       * You could read information from the environment to decide whether
       * the user has permission to start a new application
       */
        return std::make_unique<yhApplication>(env);
      }, "/", "favicon.ico");
      server.addEntryPoint(EntryPointType::Application, [](const WEnvironment &env) {
        return std::make_unique<yhApplication>(env);
      }, "/oauth/authorize", "favicon.ico");
      server.start();
    } catch (WServer::Exception& e) {
      logcpperror << "Error starting Wt web server: "<< e.what() << ENDL;
      bCont = false;
    } catch (std::exception& e) {
      logcpperror << "exception: " << e.what() << ENDL;
      bCont = false;
    }
    if(sparams) delete sparams;
    setDeviceReporting();

    unsigned long sleepage;
    while(getbCont()) {
      reportStatistics();
      timer_loop(false);
      std::unique_lock lock(main_mtx);
      sleepage = timer_getSleepage(1000); //если таймеров нет, то будем спать 1 секунду
      logcppdebug << "Main thread goes to sleep for "<<sleepage<<" ms" << ENDL;
      // wait освобождает мьютекс и засыпает
      main_cv.wait_for(lock,std::chrono::milliseconds(sleepage));
      // После пробуждения мьютекс снова захвачен нами
      logcppdebug << "Main thread waked up!" << ENDL;;
    }
    server.stop();
    cleanUp();
    return 0;
}
