#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

// Структура, описывающая вилку
struct Fork
{
    pthread_mutex_t mutex; // мьютекс доступа к вилке
    int num;               // номер вилки, используется только для отладки
};

// Инициализировать вилку f
void fork_init(struct Fork* f, int number)
{
    pthread_mutex_init(&f->mutex, NULL);
    f->num = number;
}

// Уничтожить вилку f
void fork_free(struct Fork* f)
{
    pthread_mutex_destroy(&f->mutex);
}

// Взять вилку f
// Если вилка занята, поток блокируется, пока вилка не освободится
void fork_take(struct Fork* f)
{
    pthread_mutex_lock(&f->mutex);
}

// Положить вилку
// Если вилка уже лежит, то ничего не делает
void fork_leave(struct Fork* f)
{
    pthread_mutex_unlock(&f->mutex);
}


// Философ
struct Philo
{
    int num;                 // Номер философа, начиная с 1
    pthread_t thread;        // Поток в котором философ выполняет свои действия
    pthread_mutex_t mutex;   // Мьютекс разграничения доступа к философу
    struct timeval last_eat; // Время последней еды (последнего начала поедания)
    int eat_count;           // Количество проведенных приемов пищи
    
    struct Fork* left_fork;  // Указатель на используемую в качестве левой вилку
    struct Fork* right_fork; // Указатель на используемую в качестве правой вилку
};

// Инициализация философа
void philo_init(struct Philo* ph, int number, struct Fork* left, struct Fork* right)
{
    ph->num = number;
    ph->left_fork = left;
    ph->right_fork = right;

    pthread_mutex_init(&ph->mutex, NULL);

    gettimeofday(&ph->last_eat, NULL);
}

// Разрушение философа
void philo_free(struct Philo* philo)
{
    pthread_mutex_destroy(&philo->mutex);
}

// Стол со всеми философами и вилками
struct Table
{
    int    philo_count;             // кол-во философов (и вилок)

    struct Fork* forks;             // массив вилок
    struct Philo* philos;           // массив философов
    
    int eat_period;                 // время на прием пищи
    int sleep_peroid;               // время на сон
    int die_period;                 // максимальное время между приемами пищи чтобы не умереть
    int min_eat;                    // минимальное кол-во приемов пищи, если 0, то не учитывается

    pthread_t monitor;              // поток наблюдения за столом и окончания эмуляции
    struct timeval emul_start;      // время начала эмуляции
    int simulation_end;             // флаг завершения эмуляции

    pthread_mutex_t output_mutex;   // мьютекс разграничения доступа к выводу сообщений
};

// Только один стол во всей эмуляции
struct Table table;

// Инициализация стола
void table_init(struct Table * table, int nPhilo, int tDie, int tEat, int tSleep, int max_eat_count)
{
    int nForks = nPhilo;
    
    table->forks = (struct Fork*)malloc(nForks * sizeof(struct Fork));
    for (int i = 0; i < nForks; i++)
        fork_init(&table->forks[i], i + 1);
        
    table->philo_count = nPhilo;
    table->philos = (struct Philo*)malloc(nPhilo * sizeof(struct Philo));
    for (int i = 0; i < nPhilo; i++)
        philo_init(&table->philos[i], i + 1, &table->forks[i], &table->forks[(i + 1) % nForks]);
        
    table->eat_period = tEat;
    table->sleep_peroid = tSleep;
    table->die_period = tDie;
    table->min_eat = max_eat_count;

    gettimeofday(&table->emul_start, NULL);
    
    table->simulation_end = 0;

    pthread_mutex_init(&table->output_mutex, NULL);
}

// Разрушение стола
void table_free(struct Table* table)
{
    for (int i = 0; i < table->philo_count; i++){
        fork_free (&table->forks[i]);
        philo_free(&table->philos[i]);
    }

    free(table->forks);
    free(table->philos);  

    pthread_mutex_destroy(&table->output_mutex);
}

// Возвращает разницу в миллисекундах между временем a и временем b
int time_diff(struct timeval* a, struct timeval* b)
{
    return (a->tv_sec - b->tv_sec) * 1000 + (a->tv_usec - b->tv_usec) / 1000;
}

// печатает сообщение msg от философа с номером nPhilo
void print(int nPhilo, char* msg)
{
    // только один поток одноврменно может выводить сообщения
    pthread_mutex_lock(&table.output_mutex); 

    // время вывода сообщения
    struct timeval curr;
    gettimeofday(&curr, NULL);

    // время от начала эмуляции в миллисекундах
    int fromBegin = time_diff(&curr, &table.emul_start);
    printf("%d %d %s\n", fromBegin, nPhilo, msg);

    pthread_mutex_unlock(&table.output_mutex);
}


// Функция потока философа
// принимает в качестве аргумента указатель на философа, возвращает NULL
void* philo_thread(void* args)
{
    struct Philo* p = (struct Philo*)args;

    // Цикл (думать - есть - спать)
    while (table.simulation_end == 0) {

        print(p->num, "is thinking");

        // Философ думает, пока не возьмет две вилки
        // Сначала левую...
        fork_take(p->left_fork);
        print(p->num, "has taken a fork");

        if (table.simulation_end)
            break;

        // Если философ один, то его левая вилка будет указывать на правую
        // поэтому взять ее еще один раз не получится (будет бесконечное ожидание)
        // В этом случае просто ждем завершения эмуляции
        // Поток наблюдения должен отследить голод этого философа и завершить эмуляцию
        if (p->right_fork == p->left_fork)
        {
            // ожидаем завершения симуляции
            while (!table.simulation_end)
                usleep(1000);

            break;
        }

        // ... затем правую
        fork_take(p->right_fork);
        print(p->num, "has taken a fork");

        if (table.simulation_end)
            break;

        // last_eat и eat_count могут читаться из несольких потоков
        // (поток философа и поток наблюдения)
        // поэтому защищаем их мьютексом философа
        pthread_mutex_lock(&p->mutex);

        gettimeofday(&p->last_eat, NULL); // время начала приема пищи
        print(p->num, "is eating");
        pthread_mutex_unlock(&p->mutex);

        // Ожидаем завершения приема пищи
        usleep(table.eat_period * 1000);
        if (table.simulation_end)
            break;

        // Кладем вилки на стол
        fork_leave(p->left_fork);
        fork_leave(p->right_fork);

        pthread_mutex_lock(&p->mutex);
        p->eat_count++;
        pthread_mutex_unlock(&p->mutex);

        // теперь сон
        print(p->num, "is sleeping");

        // ожидаем завершение сна
        usleep(table.sleep_peroid * 1000);
        if (table.simulation_end)
            break;
    }
    
    // На случай если симуляция остановилась внутри цикла
    // освобождаем (кладем на стол) вилки
    fork_leave(p->left_fork);
    fork_leave(p->right_fork);

    return NULL;
}

// функция потока наблюдения за столом (за философами)
// Функция потока может остановить эмуляцию
void* monitor_thread(void* args)
{
    while (1)
    {
        // текущее время наблюдения
        struct timeval curr;
        gettimeofday(&curr, NULL);

        int nFullPhilos = 0; // количество сытых философов

        // Наблюдаем за всеми философами
        for (int i =0; i < table.philo_count; i++)
        {
            struct Philo* ph = &table.philos[i]; // текущий наблюдаемый философ
            pthread_mutex_lock(&ph->mutex);      // получаем доступ, только этот поток может менять переменные филосософа

            // время после последнего приема пищи
            int time_after_int = time_diff(&curr, &ph->last_eat);

            // проверка смерти от голода
            if (time_after_int > table.die_period)
            {
                print(table.philos[i].num, "died");

                // остановка симуляции
                table.simulation_end++;
                pthread_mutex_unlock(&ph->mutex);
                return NULL;
            }

            // Сытый ли текущий философ
            if (table.min_eat && (ph->eat_count >= table.min_eat))
                nFullPhilos++;

            pthread_mutex_unlock(&ph->mutex);
        }

        // Если все философы сыты, остановка симуляции
        if (nFullPhilos == table.philo_count)
        {
            //print(0, "all complete");

            table.simulation_end++;
            return NULL;
        }
    }
}

// функция перевода строки в число
int itoa(char* t)
{
    int res = 0;
    while (*t) {
        if (*t >='0'  && *t <='9')
            res = res * 10 + *t - '0';
        t++;
    }
    return res;
}

int main(int argc, char** argv)
{
    // инициализация стола в зависимости от аргументов командной строки
    if (argc == 5)
        table_init(&table, itoa(argv[1]), itoa(argv[2]), itoa(argv[3]), itoa(argv[4]), 0);
    else if (argc == 6)
        table_init(&table, itoa(argv[1]), itoa(argv[2]), itoa(argv[3]), itoa(argv[4]), itoa(argv[5]));
    else
        return 1;

    // Запуск потоков философов
    for (int i = 0; i < table.philo_count; i++) {
        pthread_create(&(table.philos[i].thread), NULL, philo_thread, &table.philos[i]);
        usleep(50);
    }

    // запуск потока наблюдения
    pthread_create(&table.monitor, NULL, monitor_thread, NULL);
        
    // ожидание завершения всех потоков
    for (int i = 0; i < table.philo_count; i++)
        pthread_join(table.philos[i].thread, NULL);
    pthread_join(table.monitor, NULL);

    // разрушение стола
    table_free(&table);
        
    return 0;
}
