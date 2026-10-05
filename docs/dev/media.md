# Медиа (для разработчиков)

Контракт снимка экрана и звука на колонки для транспорта Iliz.

Команды приходят в MQTT `pc/{token}/cmd`. Ответы уходят в `pc/{token}/evt`.

Снимок и звук передаются по HTTP на API-сервер из поля `server` в `config.json`.

## Снимок экрана

Кнопка с `action` `system` и `value` `shot`. В MQTT это команда `type` `button` и `label` этой кнопки.

Клиент снимает экран и отправляет:

`POST {server}/api/device/shot`

Тело — JPEG.

Заголовки:

- `Content-Type: image/jpeg`
- `X-Device-Token: {token устройства}`
- `X-Cmd-Id: {id команды}`

После успешного POST в `evt`:

```json
{"id":"c1","ok":true,"shot":true}
```

Если снимок или загрузка не удались:

```json
{"id":"c1","ok":false,"error":"shot failed"}
```

Сервер сопоставляет `X-Cmd-Id` с чатом бота Iliz и отправляет туда фото. `evt` сообщает, что клиент закончил попытку.

## Звук на колонки этого ПК

Сервер сохраняет файл и публикует:

```json
{"id":"a1","type":"audio","url":"https://host/api/device/audio/a1.ogg"}
```

`url` начинается с адреса API-сервера из `config.json`. Другой хост клиент не скачивает.

Клиент скачивает файл, играет его через `bass.dll` и `bassopus.dll` и отвечает:

```json
{"id":"a1","ok":true}
```

или

```json
{"id":"a1","ok":false,"error":"audio failed"}
```

Новый ролик останавливает предыдущий.

## Own bot

Снимок уходит в Telegram методом `sendPhoto`. Voice и audio скачиваются методом `getFile` с `api.telegram.org`.
