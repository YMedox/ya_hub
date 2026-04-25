#include <Wt/Auth/PasswordVerifier.h>
#include <Wt/Auth/HashFunction.h>
#include <Wt/WServer.h>
#include "ya_hub.hpp"
#include "version.h"

cUserCredentials *user_cred;  //вспомогательный объект, на случай если в дебаг-версии передаются параметры для авторизации
std::string domain;           //домен из ya_hub.conf

// версия программы. Хранится в файле version.h
// если не определена ни одна из констант, выдаст ошибку компиляции.
std::string getVersion() {
  std::string ret = "Version " + std::string(_v_.version) + ". Built on \"" + std::string(_v_.machine) + "\" " + std::string(_v_.date);
#ifdef __DEBUG__
  std::string ret1 =  ". Debug version";
#else
  #ifdef __RELEASE__
    std::string ret1 =  ". Release version";
  #endif
#endif
  return ret + ret1 +
         ". Version of Wt library: " + WT_VERSION_STR +
         ". Version of MySQL connector: " + std::to_string(MYSQL_CONCPP_VERSION_MAJOR) + "." + std::to_string(MYSQL_CONCPP_VERSION_MINOR) + "." + std::to_string(MYSQL_CONCPP_VERSION_MICRO) + ".";
}


// класс для удобства чтения групп ключей из конфигурационного файла. Определен в ya_hub.hpp
cGroupReader::cGroupReader() { lines_ = 0; values_ = nullptr; }
cGroupReader::~cGroupReader() {
  logcppdebug<<"Destroying instance of cGroupReader"<<ENDL;
  for(gsize i=0;i<lines_;i++) {
    free(keys_[i]);
    if(values_) free(values_[i]);
  }
  if(lines_) {
    free(keys_);
    if(values_) free(values_);
  }
  lines_ = 0;
}
void cGroupReader::fillKeys(cIniObject *ini, const char *group) {
  if(lines_) {
    logcpperror<<"fillKeys is allowed only once!"<<ENDL;
    return;
  }
  keys_ = ini->getKeys(group, &lines_);
  if(!lines_) logcppwarn<<"No keys in group "<<group<<ENDL;
}
gsize cGroupReader::getSize() { return int(lines_); }
void cGroupReader::fillValues(cIniObject *ini, const char *group) {
  if(lines_) {
    values_ = (char**)malloc(sizeof(char*)*lines_);
    for(gsize i=0;i<lines_;i++) {
      values_[i] = ini->getString(group, keys_[i], false);
      logcppdebug<<keys_[i]<<" = "<<(values_[i]?values_[i]:"")<<ENDL;
    }
  } else {
    logcpperror<<"No values or group "<<group<<" in conf file."<<ENDL;
  }
}
void cGroupReader::fillAll(cIniObject *ini, const char *group) {
  fillKeys(ini, group);
  fillValues(ini, group);
}
bool cGroupReader::checkInd(gsize ind) {
  if(ind >= 0 && ind < lines_) { return true; }
  else {logcpperror<<"Wrong index passed: "<<ind<<ENDL; }
  return false;
}
char *cGroupReader::getValue(gsize ind) {
  if(checkInd(ind)) if(values_) return values_[ind];
  return nullptr;
}
char *cGroupReader::getKey(gsize ind) {
  if(checkInd(ind)) return keys_[ind];
  return nullptr;
}

// вспомогательная функция
std::string sqlMess(std::string& sql) {
      return "Executing statement: "+sql+ENDL;
}

// функция исполнения произвольных запросов к базе.
RowResult runSQLStatement(std::string sql) {
    RowResult res;
    try {
      Session mySession(*mysqlSettings);
      logcppdebug<<sqlMess(sql);
      res = mySession.sql(sql).execute();
    } catch(const mysqlx::Error &err) {
      logcpperror<<err.what()<<ENDL;
    } catch(const std::exception &err) {
      logcpperror<<err.what()<<ENDL;
    }
    return res;
  }

// функция получения строкового значения из базы данных
std::string getSQLStringValue(std::string sql) {
   try {
      Session mySession(*mysqlSettings);
      logcppdebug<<sqlMess(sql);
      RowResult res = mySession.sql(sql).execute();
      if(res.count()) {
        Row row = res.fetchOne();
        return std::string(row[0]);
      } else {
        logcppdebug<<"No data: "<<sql<<ENDL;
      }
    } catch(const mysqlx::Error &err) {
      logcpperror<<err.what()<<ENDL;
    } catch(const std::exception &err) {
      logcpperror<<err.what()<<ENDL;
    }
  return "";
}

// возвращает названия колонок из возвращаемого набора данных из mysql.
std::map<std::string, int> getColumnsNames(RowResult& res) {
  std::map<std::string, int> ret;
  for(unsigned int i=0;i<res.getColumnCount();i++) {
    ret.insert(std::make_pair(res.getColumn(i).getColumnName(), i));
    logcppdebug<<res.getColumn(i).getColumnName()<<" "<<i<<ENDL;
  }
  return ret;
}

// возвращает размер поля базы данных
int getDbFieldSize(std::string table, std::string column) {
  std::string sql = "SELECT character_maximum_length FROM information_schema.columns WHERE table_schema = Database() AND table_name='"+table+"' AND column_name='"+column+"'";
  auto res = runSQLStatement(sql);
  if(res.count()) return int(int64_t(res.fetchOne()[0]));
  return 0;
}

// отправка email. Выполняется внешней программой
bool sendMail(std::string mailto, std::string subject, std::string body) {
  if(mailcmd.length()) {
    std::string cmd = mailcmd + " " + mailto + " " + "\"" + subject + "\"  \"" + body + "\"";
    logcppdebug<<cmd<<ENDL;
    if(system(cmd.c_str()) < 0) { logcpperror<<"Error sending email to "<<mailto<<ENDL; return false; }
  }
  return true;
}

// получение хоста и порта для сообщения
std::string getHostForEmail() {
//  int port = Wt::WServer::instance()->httpPort();
//  std::string ret = "http" + std::string(port==443?"s":"") + "://your_site.ru:"+ std::to_string(port); //такая конструкция годится только для прямого соединения, без NAT
    return "https://" + domain + "/?";
//  return ret;
}

// отправка активационного имэйл. Хотя и не нужно, так как в текущей реализации пользователь только один
void sendActivateEmail(std::string email, std::string active_hex) {
  sendMail(email, "Регистрация на ymhub.ru", "Для активации Вашего аккаунта пройдите по ссылке <a href="+getHostForEmail()+"activate="+active_hex+"> активация </a>");
  logcppinfo<<"Activation email sent to "<<email<<ENDL;
}

// отправка письма при изменении пароля
bool sendChangePasswordEmail(std::string email, std::string login, std::string active_hex) {
  if(sendMail(email, "Изменение пароля для "+login+" на ymhub.ru", "Для изменения пароля Вашего аккаунта "+login+" пройдите по ссылке <a href="+getHostForEmail()+"changepass="+active_hex+"> смена пароля </a>")) {
    logcppinfo<<"Activation email sent to "<<email<<ENDL;
    return true;
  }
  return false;
}

// класс инкапсулирующий функции хэширования и проверки паролей из библиотеки Wt
class PasswordManager {
private:
    Wt::Auth::PasswordVerifier verifier_;
    std::string hashSalt;
public:
    PasswordManager() {
        // Инициализируем верификатор с bcrypt
        verifier_.addHashFunction(std::make_unique<Wt::Auth::BCryptHashFunction>());
        hashSalt.clear();
    }
    // Хэшируем пароль
    std::string hashPassword(const char* plainPassword) {
        Wt::Auth::PasswordHash hash_ = verifier_.hashPassword(std::string(plainPassword));
        hashSalt = hash_.salt();
        return hash_.value();
    }
    // Возвращаем соль. Важно: она генерируется при хэшировании.
    std::string getSaltAfterHashing() {
       return hashSalt;
    }
    // Проверяем пароль против хэша
    bool verifyPassword(const std::string& plainPassword, std::string& storedSalt, const std::string& storedHash) {
        Wt::Auth::PasswordHash hash_("bcrypt", storedSalt, storedHash);
        return verifier_.verify(plainPassword, hash_);
    }
};

// Главная функция выполняет все манипуляции с логинами. Может быть вызвана из разных мест.
// Пароль - нешифрованный, функция его зашифрует.
int manageCommand(int cmd, const char *login, const char *pass, const char *email, bool bSuperUser) {
//   logcppdebug<<"cmd="<<cmd<<", login="<<login<<", pass="<<pass<<", email="<<email<<", bSuperUser="<<bSuperUser<<ENDL;
   try {
      Session mySession(*mysqlSettings);
      logcppdebug<<"MySQL session started"<<ENDL;
      Schema myDB = mySession.getDefaultSchema();
      logcppdebug<<"Schema selected "<<myDB.getName()<<ENDL;
      Table acc  = myDB.getTable("account");
      RowResult res = acc.select("username", "sitepw", "salt", "active_hex", "email", "status")
                         .where("username = :param")
                         .orderBy("username")
                         .bind("param", login).execute();
      switch(cmd) {
        case YAH_FINDLOGIN:
            if(res.count() > 0) {
              return YAH_OK;
            }
            return YAH_ERROR;
        case YAH_NEWLOGIN:
            if(res.count() > 0) { //хорошо бы еще проверять что записей больше 1
               logcpperror"Login "<<login<<" already exists in db"<<ENDL;
               return YAH_EXISTS;
            } else {
               PasswordManager pm;
               std::string sitepw     = pm.hashPassword(pass);
               std::string salt       = pm.getSaltAfterHashing();
               std::string active_hex = pm.hashPassword(salt.c_str());
               try {
                 acc.insert("username", "sitepw", "salt", "active_hex", "email", "status", "superuser")
                    .values(login, sitepw.c_str(), salt.c_str(), active_hex.c_str(), email, bSuperUser?1:0, bSuperUser?1:0).execute();
                 logcppinfo<<"Login "<<login<<" added"<<ENDL;
                 sendActivateEmail(std::string(email), active_hex);
               } catch(const mysqlx::Error &err) {
                 logcpperror<<"Failed adding: "<<err.what()<<ENDL;
                 return YAH_ERROR;
               } catch (const std::exception &err) {
                 logcpperror<<"Failed adding "<<login<<": "<<err.what()<<ENDL;
                 return YAH_ERROR;
              }
            }
            break;
        case YAH_CHANGEPASS:
            if(res.count() == 0) { //хорошо бы еще проверять что записей больше 1
               logcpperror"Login "<<login<<" not found in db"<<ENDL;
               return YAH_NOTFOUND;
            } else {
              try {
                Row row = res.fetchOne();
                PasswordManager pm;
                std::string sitepw = pm.hashPassword(pass);
                std::string salt   = pm.getSaltAfterHashing();
                acc.update()
                   .set("sitepw", sitepw)
                   .set("salt", salt)
                   .where("username = :login")
                   .bind("login", login).execute();
                logcppinfo<<"Password for "<<login<<" updated"<<ENDL;
              } catch(const mysqlx::Error &err) {
                logcpperror<<"Update failed: "<<err.what()<<ENDL;
                return YAH_ERROR;
              } catch(const std::exception &err) {
                logcpperror<<"Update failed: "<<err.what()<<ENDL;
                return YAH_ERROR;
              }
            }
            break;
        case YAH_REMOVE:
            if(res.count() == 0) {
               logcpperror<<"Login "<<login<<" doesn't exist in db"<<ENDL;
               return YAH_NOTFOUND;
            } else {
               Table dev  = myDB.getTable("user_devices");
               try {
                 acc.remove()
                    .where("username == :login")
                    .bind("login", login).execute();
                 logcppinfo<<"Login "<<login<<" removed"<<ENDL;
               } catch(const mysqlx::Error &err) {
                 logcpperror<<"Failed removing "<<login<<": "<<err.what()<<ENDL;
                 return YAH_ERROR;
               } catch (const std::exception &err) {
                 logcpperror<<"Failed removing "<<login<<": "<<err.what()<<ENDL;
                 return YAH_ERROR;
              }
            }
            break;
        case YAH_SITEAUTH:
            if(res.count() == 0) {
               logcppinfo<<"Auth attempt with wrong login "<<login<<ENDL;
               return YAH_NOTFOUND;
            } else {
                Row row = res.fetchOne();
                PasswordManager pm;
                std::string salt   = std::string(row[2]);
                if(int(row[5]) != 0) {
                  if(pm.verifyPassword(pass, salt, std::string(row[1]))) {
                    logcppinfo<<"Login "<<login<<" authorized"<<ENDL;
                    return YAH_OK;
                  } else {
                    logcppinfo<<"Login "<<login<<" not authorized, wrong password"<<ENDL;
                    return YAH_WRONGPASS;
                  }
                } else {
                  logcppinfo<<"Login "<<login<<" not activated. Sending activation email"<<ENDL;
                  sendActivateEmail(std::string(row[4]), std::string(row[3]));
                  return YAH_NOTACTIVE;
                }
            }
            break;
        case YAH_GETINFO:
            logcpperror<<"Command "<<cmd<<" not implemented yet"<<ENDL;
            return YAH_ERROR;
        default:
            logcpperror<<"Unknown command received "<<cmd<<". Nothing to do"<<ENDL;
            return YAH_ERROR;
            break;
      }
    } catch(const mysqlx::Error &err) {
      logcpperror<<err.what()<<ENDL;
      return YAH_ERROR;
    } catch(const std::exception &err) {
      logcpperror<<err.what()<<ENDL;
      return YAH_ERROR;
    }

  return YAH_OK;
}

// функция обработки аргументов программы передаваемых в командной строке
void manageArgv(int argc, char *argv[], const char *config_file) {
    int cmd = -1, acount = 1;
//    char *passw = nullptr;
    user_cred = nullptr;
    for(acount=1;acount<argc;acount++) {
       if(strcmp(argv[acount], "-s")==0) {         //Не выводить сообщения на экран
         if(logger) {
           cLogParams p;
           logger->getParams(&p);
           p.sink_cout = false;
           logger->setParams(&p);
         }
       } else if(strcmp(argv[acount], "-n")==0) {  //Новый пользователь
         cmd = YAH_NEWLOGIN;
         break;
       } else if(strcmp(argv[acount], "-m")==0) {  //Смена пароля
         cmd = YAH_CHANGEPASS;
         break;
       } else if(strcmp(argv[acount], "-r")==0) {  //Удаление пользователя
         cmd = YAH_REMOVE;
         break;
       } else if(strcmp(argv[acount], "-v")==0) {  //Показать версию програимы
         //puts(getVersion().c_str());             //Необязательно показывать. Последнее сообщение в логе будет с версией как раз
         cleanUp();
         exit(0); //при выполнении команды всегда выходим из программы.
#ifdef __DEBUG__
       } else if(strcmp(argv[acount], "-u")==0) {  // заходим с логином и паролем сразу. Только для DEBUG версии
         user_cred = new cUserCredentials;
         user_cred->username = argv[++acount];
         user_cred->password = argv[++acount];
#endif // __DEBUG__
       }
    }
    if(cmd > 0) { //найдена команда
      if(acount < argc-(cmd==YAH_REMOVE?1:(cmd==YAH_NEWLOGIN?3:2))) {
        const char *l = acount<argc?argv[++acount]:"";
        const char *p = acount<argc?argv[++acount]:"";
        const char *e = acount<argc?argv[++acount]:"-";
        manageCommand(cmd, l, p, e, true); //в командной строке всегда считаем, что работаем с суперюзерами
      } else {
        std::cout<<"Wrong command passed.\nUsage: "<<argv[0]<<" [-v] [-s] [-c] [-n login pass email] [-m login pass] [-r login]"<<std::endl;
      }
      cleanUp();
      exit(0); //при выполнении команды всегда выходим из программы.
    }
}

// таймаут ожидания запертого mutex
// этот mutex используется для формирования ответа от устройства. Чтобы они не смешивались от разных устройств
void cWaitForResponse::wait(uint32_t seconds) {
    auto start = std::chrono::steady_clock::now();
    while(1) {
      usleep(1000);
      mtx.lock();
      if ((std::chrono::steady_clock::now() - start > std::chrono::seconds(seconds)) || bReady)  {
        bReady = false; mtx.unlock(); break;
      }
      mtx.unlock();
   }
}
// разрешение дальнейшей работы
void cWaitForResponse::allow() {
    mtx.lock(); bReady = true; mtx.unlock();
}


// класс используется для "превращения" асинхронных запросов в синхронные. Просто пока нет ответа от сервера, зажимаем bReady и ждем пока обработчик ответа не отпустит ее
// но в пределах таймаута
std::string deviceResponse;  // здесь будет ответ от устройства
void cWaitAnswer::setDeviceResponse(std::string resp) { mtx.lock(); deviceResponse = resp; mtx.unlock(); }
std::string cWaitAnswer::getDeviceResponse() { mtx.lock(); std::string ret = deviceResponse; mtx.unlock(); return ret; }
cWaitAnswer waitForResponse; // единый глобальный экземпляр обертки для mutex, который управляет записью deviceResponse

// вычисляет используемую программой память
void calcMemorySize(double &vm_usage, double &resident_set) {
   using std::ios_base;
   using std::ifstream;
   using std::string;
   vm_usage     = 0.0;
   resident_set = 0.0;
   // 'file' stat seems to give the most reliable results
   ifstream stat_stream("/proc/self/stat",ios_base::in);
   // dummy vars for leading entries in stat that we don't care about
   string pid, comm, state, ppid, pgrp, session, tty_nr;
   string tpgid, flags, minflt, cminflt, majflt, cmajflt;
   string utime, stime, cutime, cstime, priority, nice;
   string O, itrealvalue, starttime;
   // the two fields we want
   unsigned long vsize;
   long rss;
   stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr
               >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt
               >> utime >> stime >> cutime >> cstime >> priority >> nice
               >> O >> itrealvalue >> starttime >> vsize >> rss; // don't care about the rest
   stat_stream.close();
   long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // in case x86-64 is configured to use 2MB pages
   vm_usage     = vsize / 1024.0;
   resident_set = rss * page_size_kb;
}


// после 2038г - переполнение. как будет работать time_t - неизвестно
inline std::string getDateFormatForDB()  { return "%Y-%m-%d %H:%M:%S"; };
std::string getDateString(const char *format) {
  time_t rawtime;
  struct tm * timeinfo;
  char buffer [80];                                // строка, в которой будет храниться текущее время
  time ( &rawtime );                               // текущая дата в секундах
  timeinfo = localtime ( &rawtime );               // текущее локальное время, представленное в структуре
  strftime (buffer, sizeof(buffer),format,timeinfo); // форматируем строку времени
  std::string ret = std::string(buffer);
  return ret;
}
// форматирование для записи в MySQL
std::string getDateStringForDB() {
  return getDateString(getDateFormatForDB().c_str());
}

// получаем рабочий каталог программы.
std::string getCurrentWorkingDirectory() {
    const size_t bufferSize = 1024;
    char buffer[bufferSize];
    if (getcwd(buffer, bufferSize) != nullptr) {
        return std::string(buffer);
    } else {
        // В случае ошибки возвращаем пустую строку
        return "";
    }
}

// Обрезает строку в начале и конце
void trimString(std::string& input) {
  try {
    input.erase(0, input.find_first_not_of(" \t\n\r"));
    input.erase(input.find_last_not_of(" \t\n\r") + 1);
  } catch (...) {
     logcpperror<<"Error while trimming string: "<<input<<ENDL;
  }
}


/**********************************************************************************************************************************
Блок преобразования русских строк к нижнему регистру. Манипулирует с локалями. При этом оставить русскую локаль просто так нельзя.
Иначе перестают работать исходящие HTPP-запросы. Возможно в них можно установить локаль отдельно.
Блок на 90% написан Алисой
**********************************************************************************************************************************/
std::wstring string_to_wstring(const std::string& str) {
    // Сначала узнаём необходимый размер буфера
    size_t len = std::mbstowcs(nullptr, str.c_str(), 0);
    if (len == static_cast<size_t>(-1)) {
        throw std::runtime_error("Ошибка преобразования строки: неверная кодировка");
    }
    // Создаём буфер нужного размера
    std::wstring result(len, L'\0');
    // Выполняем преобразование
    len = std::mbstowcs(&result[0], str.c_str(), len);
    if (len == static_cast<size_t>(-1)) {
        throw std::runtime_error("Ошибка преобразования строки");
    }
    result.resize(len); // Урезаем до реальной длины
    return result;
}

std::string wstring_to_string(const std::wstring& wstr) {
    // Узнаём необходимый размер буфера
    size_t len = std::wcstombs(nullptr, wstr.c_str(), 0);
    if (len == static_cast<size_t>(-1)) {
        throw std::runtime_error("Ошибка преобразования wstring в string");
    }
    // Создаём буфер
    std::string result(len, '\0');
    // Выполняем преобразование
    len = std::wcstombs(&result[0], wstr.c_str(), len);
    if (len == static_cast<size_t>(-1)) {
        throw std::runtime_error("Ошибка преобразования");
    }
    result.resize(len);
    return result;
}
//функция на 90% написана Алисой и 2 функции выше также
void to_lower_russian(std::string& input) {
    std::locale current_locale;
  try {
    //logcppdebug<<"Name of current locale: "<<current_locale.name()<<ENDL;
    std::locale::global(std::locale("ru_RU.UTF-8"));  //если установить глобально, перестают работать исходящие http-запросы.
    std::wstring text = string_to_wstring(input);
    // Преобразование
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return std::towlower(c);});
    input = wstring_to_string(text);
    //logcppdebug<<"Name of current locale: "<<current_locale.name()<<ENDL;
    std::locale::global(std::locale(current_locale.name()));
  } catch (const std::runtime_error& e) {
        // Логируем ошибку преобразования
        logcpperror << "Error converting string: '"<<input<<"'" << e.what() << ENDL;
        // Восстанавливаем локаль даже при ошибке
        try {
            std::locale::global(std::locale(current_locale.name()));
        }
        catch (...) {
            // Игнорируем ошибки при восстановлении локали
        }
  }
}
/**********************************************************************************************************************************
Конец блока преобразования русской строки к нижнему регистру.
**********************************************************************************************************************************/
