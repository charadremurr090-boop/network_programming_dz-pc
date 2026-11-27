# network_programming_prac

# prac07 (практика 26.11) - проверка сервера, вывод клиента:

<img width="1271" height="369" alt="image" src="https://github.com/user-attachments/assets/4d597d64-f4f6-4f28-bf80-217eb9ca4ed3" />

# prac05 (практика 20.11) - проверка сервера:
<img width="1567" height="820" alt="проверка" src="https://github.com/user-attachments/assets/f08f184f-a77c-413d-9f6b-d4d93039f647" />

Схема вызовов: CRUD для /users

| Операция | Метод | URL | Тело запроса | Успех | Ошибки |
|---|---|---|---|---|---|
| Список | GET | /users | нет | 200 | нет |
| Один пользователь | GET | /users/42 | нет | 200 | 404 |
| Создать | POST | /users | {"name":"Ada"} | 201 | 400, 409 |
| Заменить | PUT | /users/42 | {"name":"Ada"} | 200 / 204 | 400, 404 |
| Изменить часть | PATCH | /users/42 | {"email":"..."} | 200 / 204 | 400, 404 |
| Удалить | DELETE | /users/42 | нет | 204 | 404 |

prac05_2 (практика 20.11) - проверка WebSocket и данных

JSON против protobuf: serialize.c
<img width="517" height="112" alt="image" src="https://github.com/user-attachments/assets/92cdc028-cefe-4df8-bfc6-3f7bdfb15842" />

Разбор кадра: ws_frame.c
<img width="347" height="322" alt="image" src="https://github.com/user-attachments/assets/304b6858-d70e-4ff9-a793-fa1375400ad4" />

Echo-сервер и клиент:
<img width="465" height="381" alt="image" src="https://github.com/user-attachments/assets/da5fdc8b-54e6-49fd-b07c-33b2f5673803" />

Чат двух клиентов:
<img width="1440" height="831" alt="image" src="https://github.com/user-attachments/assets/127842dd-3da4-445e-95f1-30dd2270b274" />

# prac04 (практика 19.11 / 17.11) - проверка сервера и ответы на вопросы:

Создаём exe через gcc и открываем три клиента:
<img width="974" height="937" alt="image" src="https://github.com/user-attachments/assets/ae3609ea-ae10-4bbd-938a-970e33c8e84f" />

Запускаем Race:

<img width="733" height="328" alt="image" src="https://github.com/user-attachments/assets/e13495cd-5806-4736-9a28-c8122e67f668" />

Ответы на вопросы:

Почему гонка не воспроизводится стабильно?

Результат зависит от того, как планировщик чередует потоки. Поток прерывается между чтением значения и записи. Поэтому каждый новый запуск – другой.

# prac03 (практика 17.11) - проверка сервера и ответы на вопросы:

server-loop

<img width="1142" height="108" alt="image" src="https://github.com/user-attachments/assets/8975d8a4-6848-411c-aea5-b06b3faf763f" />

server-epull

<img width="1429" height="820" alt="image" src="https://github.com/user-attachments/assets/0905d00f-f4ac-4e76-84e6-252dc983df98" />

Ответы на вопросы:

Сравнение кода select и epoll
select использует битовые маски, требует пересборки набора в цикле и перебирает все дескрипторы O(n). epoll хранит список в ядре, мы только регистрируем сокеты, а на выходе получаем массив только готовых событий O(1).

Почему ET требует цикла?
В режиме ET событие приходит только один раз — при изменении состояния. Если не вычитать всё до EAGAIN, остаток данных «зависнет» в буфере, и ядро не пришлёт новое событие, пока не поступят новые данные.
