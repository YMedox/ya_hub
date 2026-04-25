#!/bin/bash
# Будет отображаться "От кого"
FROM=your_email_from@mail.ru
# Кому
MAILTO=$1
# Тема письма
NAME=$2
# Тело письма
BODY=$3
# Скрипт легко адаптируется для любых почтовых серверов
SMTPSERVER=smtp.mail.ru
# Логин и пароль от учетной записи 
SMTPLOGIN=your_login@mail.ru
SMTPPASS=your_token

# Узел опроса
ip="ya.ru"
# Кол-во пингов
count=3
# инициализация переменной результата, по умолчанию считается, что связь уже есть
status=disconnected


while [ true ]; do
  result=$(ping -c ${count} ${ip} 2<&1| grep -icE 'unknown|expired|unreachable|time out|неизвестно')
  #echo $result
    if [ "$status" = disconnected -a "$result" -eq 0 ]; then
	# Меняем статус, чтоб сообщение не повторялось до смены переменной result
	status=connected
	# Вывод результата на экран
	echo `date +%Y.%m.%d_%H:%M:%S`' Связь есть'
        # Отправляем письмо
        if grep -q $'\n' <<< "$BODY"; then
          echo $BODY > file_send
          sendemail -f $FROM -t $MAILTO -o message-charset=utf-8 -o tls=yes  -u $NAME -s $SMTPSERVER -o -xu $SMTPLOGIN -xp $SMTPPASS -o message-file=file_send
        else
          sendemail -f $FROM -t $MAILTO -o message-charset=utf-8 -o tls=yes  -u $NAME -s $SMTPSERVER -o -xu $SMTPLOGIN -xp $SMTPPASS -m $BODY
        fi
        sendemail -f $FROM -t $MAILTO -o message-charset=utf-8 -o tls=yes  -u $NAME -s $SMTPSERVER -o -xu $SMTPLOGIN -xp $SMTPPASS -m $BODY
        exit
    fi
  echo Следующая попытка
  # 15 сек задержка
  sleep 15
done 


