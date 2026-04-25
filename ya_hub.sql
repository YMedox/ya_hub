--
-- Table structure for table `account`
--

DROP TABLE IF EXISTS `account`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `account` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `username` varchar(20) NOT NULL,
  `sitepw` varchar(100) NOT NULL,
  `salt` varchar(100) NOT NULL,
  `active_hex` varchar(100) NOT NULL,
  `email` varchar(20) NOT NULL,
  `status` int NOT NULL,
  `superuser` smallint NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `id` (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=27 DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;



--
-- Table structure for table `devices`
--

DROP TABLE IF EXISTS `devices`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `devices` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `id_for_yandex` char(30) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `device` blob,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`id_for_yandex`)
) ENGINE=InnoDB AUTO_INCREMENT=35 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `devices`
--

LOCK TABLES `devices` WRITE;
/*!40000 ALTER TABLE `devices` DISABLE KEYS */;
INSERT INTO `devices` VALUES (1,'FR2Light11',_binary '{\n  \"name\": \"Свет 1\",\n  \"description\": \"1я кнопка выключателя света\",\n  \"room\": \"гостиная\",\n  \"type\": \"devices.types.light.ceiling\",\n  \"interval\": 7300,\n  \"status_info\": {\n    \"reportable\": true\n  },\n  \"device_info\": {\n  	\"manufacturer\": \"ali\",\n  	\"model\": \"zigbee\",\n  	\"hw_version\": \"1.0\",\n  	\"sw_version\": \"2.0\"\n   },\n   \"request\": {\n     \"host\": \"http://homeassistant.local:8123/\",\n     \"headers\" : {\n         \"Authorization\": \"Bearer from homeassistant\",\n         \"Content-Type\": \"application/json\"\n     }\n   },\n   \"api\": {\n     \"devices.capabilities.on_off\": [\n       { \"instance\": \"on\",\n         \"query\": {\n           \"method\": \"GET\",\n           \"url\": \"api/states/light.living_room1\"\n         },         \n         \"action\": {\n           \"method\": \"POST\",\n           \"false\":  \"api/services/light/turn_off\",\n           \"true\":  \"api/services/light/turn_on\",\n           \"data_false\": { \"entity_id\": \"light.living_room1\" },\n           \"data_true\":  { \"entity_id\": \"light.living_room1\" }\n         },\n         \"response\": {\n           \"key\": \"state\",\n           \"on\": true,\n           \"off\": false\n         }\n       }\n     ]\n   },\n   \"capabilities\": [\n     {\n            \"type\": \"devices.capabilities.on_off\",\n            \"retrievable\": true,\n            \"reportable\": true\n     }\n   ]\n}');
/*!40000 ALTER TABLE `devices` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `mqttstat`
--

DROP TABLE IF EXISTS `mqttstat`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `mqttstat` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `id_for_yandex` char(30) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `state` char(250) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `updated` timestamp NULL DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`id_for_yandex`)
) ENGINE=InnoDB AUTO_INCREMENT=7 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;


--
-- Table structure for table `sb_codes`
--

DROP TABLE IF EXISTS `sb_codes`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `sb_codes` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `state` char(15) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `statement` char(255) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`state`)
) ENGINE=InnoDB AUTO_INCREMENT=9 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sb_codes`
--

LOCK TABLES `sb_codes` WRITE;
/*!40000 ALTER TABLE `sb_codes` DISABLE KEYS */;
INSERT INTO `sb_codes` VALUES (1,'action1','{ \"capabilities\": [{ \"type\": \"devices.capabilities.on_off\", \"state\": { \"instance\": \"on\", \"value\": \"$value\" } }] }'),(2,'state1','{ \"instance\": \"on\", \"value\": \"$on_off1\" }'),(3,'state2','{ \"instance\": \"on\", \"value\": \"$on_off2\" }'),(4,'state3','{ \"instance\": \"on\", \"value\": \"$on_off3\" }'),(5,'state4','{ \"instance\": \"temperature\", \"value\": \"$celsius\" }'),(6,'state5','{ \"instance\": \"humidity\", \"value\": \"$percent\" }'),(7,'state6','{ \"response\": [{ \"instance\": \"temperature\", \"value\": \"Температура $celsius\" }, { \"instance\": \"humidity\", \"value\": \"Влажность $percent\" } ] }'),(8,'action2','{ \"capabilities\": [{ \"type\": \"devices.capabilities.toggle\", \"state\": { \"instance\": \"mute\", \"value\": \"$value\" } }] }');
/*!40000 ALTER TABLE `sb_codes` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sb_requests`
--

DROP TABLE IF EXISTS `sb_requests`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `sb_requests` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `room` char(32) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `entity` char(64) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `ids_for_yandex` char(240) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `action` char(15) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `state` char(15) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`room`,`entity`)
) ENGINE=InnoDB AUTO_INCREMENT=33 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sb_requests`
--

LOCK TABLES `sb_requests` WRITE;
/*!40000 ALTER TABLE `sb_requests` DISABLE KEYS */;
INSERT INTO `sb_requests` VALUES (1,'гостиная','свет','FR2Light11, FR2Light12','action1','state1'),(2,'гостиная','свет один','FR2Light11','action1','state1'),(3,'гостиная','свет два','FR2Light12','action1','state1'),(4,'гостиная','люстра','FR2Light11, FR2Light12','action1','state2'),(5,'гостиная','люстра один','FR2Light11','action1','state2'),(6,'гостиная','люстра два','FR2Light12','action1','state2'),(7,'гостиная','штора','FR2CurtainR','action1','state3'),(8,'гостиная','погода','FR2THCROTON','','state6'),(9,'гостиная','температура','FR2THCROTON','','state4'),(10,'гостиная','влажность','FR2THCROTON','','state5'),(11,'кухня','свет','FKitchenLight','action1','state1'),(12,'кухня','люстра','FKitchenLight','action1','state1'),(13,'балкон','температура','FBALCTH1','','state4'),(14,'балкон','влажность','FBALCTH1','','state5'),(15,'балкон','погода','FBALCTH1','','state6'),(16,'улица','температура','FR2OutTH1','','state4'),(17,'улица','влажность','FR2OutTH1','','state5'),(18,'улица','погода','FR2OutTH1','','state6'),(19,'спальня','свет','FR1Light11, FR1Light12','action1','state1'),(20,'спальня','свет один','FR1Light11','action1','state1'),(21,'спальня','свет два','FR1Light12','action1','state1'),(22,'спальня','люстра','FR1Light11, FR1Light12','action1','state2'),(23,'спальня','люстра один','FR1Light11','action1','state2'),(24,'спальня','люстра два','FR1Light12','action1','state2'),(25,'коридор','свет','FHWLight1','action1','state1'),(26,'кордиор','люстра','FHWLight1','action1','state1'),(27,'кухня','штора','FKitchenRB1m','action1','state1'),(28,'спальня','ночники','FR1Lamp1, FR1Lamp2','action1','state1'),(29,'спальня','ночник один','FR1Lamp1','action1','state1'),(30,'спальня','ночник два','FR1Lamp2','action1','state1'),(31,'гостиная','телевизор','FR2IRRemote','action1',''),(32,'гостиная','звук','FR2IRRemote','action2','');
/*!40000 ALTER TABLE `sb_requests` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sb_synonyms`
--

DROP TABLE IF EXISTS `sb_synonyms`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `sb_synonyms` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `entity_src` char(64) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `entity_dst` char(64) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`entity_src`)
) ENGINE=InnoDB AUTO_INCREMENT=17 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sb_synonyms`
--

LOCK TABLES `sb_synonyms` WRITE;
/*!40000 ALTER TABLE `sb_synonyms` DISABLE KEYS */;
INSERT INTO `sb_synonyms` VALUES (1,'livingroom','гостиная'),(2,'kitchen','кухня'),(4,'balcony','балкон'),(5,'hallway','коридор'),(6,'bedroom','спальня'),(7,'outdoor','улица'),(8,'light','свет'),(9,'light1','свет один'),(10,'light2','свет два'),(11,'nightlights','ночники'),(12,'nightlight1','ночник один'),(13,'nightlight2','ночник два'),(14,'temperature','температура'),(15,'humidity','влажность'),(16,'weather','погода');
/*!40000 ALTER TABLE `sb_synonyms` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `statuses`
--

DROP TABLE IF EXISTS `statuses`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `statuses` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `id_for_yandex` char(30) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `state` blob,
  `requestor` char(20) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `updated` timestamp NULL DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`id_for_yandex`,`requestor`)
) ENGINE=InnoDB AUTO_INCREMENT=100 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;


--
-- Table structure for table `tokens`
--

DROP TABLE IF EXISTS `tokens`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `tokens` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `token` char(32) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `refresh` char(32) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `username` char(20) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `client_id` char(48) COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `main_ind` (`client_id`),
  KEY `token_ind` (`token`)
) ENGINE=InnoDB AUTO_INCREMENT=44 DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

