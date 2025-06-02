#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h> // Para EEXIST

// Definición de las tuberías con nombre
#define FIFO_MESSAGE "fifo_message"
#define FIFO_ENCRYPT "fifo_encrypt"
#define FIFO_DECRYPT "fifo_decrypt"
#define FIFO_RESULT "fifo_result"

// Tamaño máximo del buffer para las cadenas
#define BUFFER_SIZE 256

// PIDs de los procesos hijos (necesarios para enviar señales)
pid_t client1_pid, client2_pid, client3_pid;

// Banderas para sincronización con señales (Servidor)
volatile sig_atomic_t client1_finished = 0;
volatile sig_atomic_t client2_finished = 0;
volatile sig_atomic_t client3_finished = 0;

// Manejador de señales para el SERVIDOR
void server_signal_handler(int signo) {
    if (signo == SIGUSR1) {
        if (waitpid(client1_pid, NULL, WNOHANG) == client1_pid) {
            client1_finished = 1;
        } else if (waitpid(client2_pid, NULL, WNOHANG) == client2_pid) {
            client2_finished = 1;
        } else if (waitpid(client3_pid, NULL, WNOHANG) == client3_pid) {
            client3_finished = 1;
        }
    }
}

// Manejador de señales para los CLIENTES
volatile sig_atomic_t start_task = 0;
void client_signal_handler(int signo) {
    if (signo == SIGUSR1) {
        start_task = 1;
    }
}


// Función para encriptar una cadena
void encrypt_string(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        str[i] += 3; // Suma 3 al valor ASCII
    }
}

// Función para desencriptar una cadena
void decrypt_string(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        str[i] -= 3; 
    }
}

// Función para invertir una cadena
void reverse_string(char *str) {
    int length = strlen(str);
    for (int i = 0; i < length / 2; i++) {
        char temp = str[i];
        str[i] = str[length - 1 - i];
        str[length - 1 - i] = temp;
    }
}

int main() {
    char original_message[BUFFER_SIZE];
    char processed_message[BUFFER_SIZE];
    int fd; 

    // 1. Configurar manejador de señales (Servidor)
    struct sigaction sa_server;
    sa_server.sa_handler = server_signal_handler;
    sigemptyset(&sa_server.sa_mask);
    sa_server.sa_flags = SA_RESTART | SA_NOCLDWAIT; // SA_NOCLDWAIT evita que el padre tenga que llamar waitpid
                                                    // SA_RESTART: reinicia las syscalls que la señal pudo interrumpir.
    if (sigaction(SIGUSR1, &sa_server, NULL) == -1) {
        perror("Servidor: sigaction");
        exit(EXIT_FAILURE);
    }

    // 2. Proceso Padre (Servidor): Crea las tuberías con nombre
    printf("Servidor (PID: %d): Creando tuberías con nombre...\n", getpid());
    if (mkfifo(FIFO_MESSAGE, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo fifo_message");
        exit(EXIT_FAILURE);
    }
    if (mkfifo(FIFO_ENCRYPT, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo fifo_encrypt");
        exit(EXIT_FAILURE);
    }
    if (mkfifo(FIFO_DECRYPT, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo fifo_decrypt");
        exit(EXIT_FAILURE);
    }
    if (mkfifo(FIFO_RESULT, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo fifo_result");
        exit(EXIT_FAILURE);
    }

    printf("Servidor: Ingrese la cadena a procesar: ");
    if (fgets(original_message, BUFFER_SIZE, stdin) == NULL) {
        perror("fgets");
        exit(EXIT_FAILURE);
    }
    original_message[strcspn(original_message, "\n")] = '\0'; 

    // 3. Crea los tres procesos hijos (Cliente 1, Cliente 2, Cliente 3)
    // Se crean todos los hijos primero, para que sus PIDs estén disponibles en el padre
    // antes de empezar la orquestación.

    client1_pid = fork();
    if (client1_pid < 0) {
        perror("fork client1");
        exit(EXIT_FAILURE);
    }
    if (client1_pid == 0) { // Código para Cliente 1
      
        struct sigaction sa_client;
        sa_client.sa_handler = client_signal_handler;
        sigemptyset(&sa_client.sa_mask);
        sa_client.sa_flags = SA_RESTART;
        if (sigaction(SIGUSR1, &sa_client, NULL) == -1) {
            perror("Cliente 1: sigaction");
            exit(EXIT_FAILURE);
        }

        printf("Cliente 1 (PID: %d): Esperando señal del servidor para iniciar...\n", getpid());
        while (!start_task) {
            pause(); // Espera la señal del servidor
        }
        start_task = 0; 

        printf("Cliente 1: Señal recibida. Abriendo %s para lectura...\n", FIFO_MESSAGE);
        if ((fd = open(FIFO_MESSAGE, O_RDONLY)) == -1) {
            perror("Cliente 1: open fifo_message");
            exit(EXIT_FAILURE);
        }
        ssize_t bytes_read = read(fd, processed_message, BUFFER_SIZE);
        close(fd);
        if (bytes_read == -1) {
            perror("Cliente 1: read fifo_message");
            exit(EXIT_FAILURE);
        }
        processed_message[bytes_read] = '\0';
        printf("Cliente 1: Cadena leída: '%s'\n", processed_message);

        if (strlen(processed_message) == 0) {
            fprintf(stderr, "Cliente 1: Cadena vacía recibida. Finalizando con error.\n");
            exit(EXIT_FAILURE);
        }

        encrypt_string(processed_message);
        printf("Cliente 1: Cadena encriptada: '%s'\n", processed_message);

        if ((fd = open(FIFO_ENCRYPT, O_WRONLY)) == -1) {
            perror("Cliente 1: open fifo_encrypt");
            exit(EXIT_FAILURE);
        }
        if (write(fd, processed_message, strlen(processed_message) + 1) == -1) {
            perror("Cliente 1: write fifo_encrypt");
            exit(EXIT_FAILURE);
        }
        close(fd);
        printf("Cliente 1: Cadena escrita en %s. Notificando al servidor y terminando.\n", FIFO_ENCRYPT);

        if (kill(getppid(), SIGUSR1) == -1) { 
            perror("Cliente 1: kill SIGUSR1 to server");
        }
        exit(EXIT_SUCCESS);
    }

    // Código del Proceso Padre (Servidor)
    // Solo continúa creando hijos si no es un proceso hijo
    if (client1_pid > 0) { 
        printf("Servidor: Cliente 1 (PID: %d) creado. Creando Cliente 2...\n", client1_pid);
        client2_pid = fork();
        if (client2_pid < 0) {
            perror("fork client2");
            exit(EXIT_FAILURE);
        }
        if (client2_pid == 0) { // Código para Cliente 2
            struct sigaction sa_client;
            sa_client.sa_handler = client_signal_handler;
            sigemptyset(&sa_client.sa_mask);
            sa_client.sa_flags = SA_RESTART;
            if (sigaction(SIGUSR1, &sa_client, NULL) == -1) {
                perror("Cliente 2: sigaction");
                exit(EXIT_FAILURE);
            }

            printf("Cliente 2 (PID: %d): Esperando señal del servidor para iniciar...\n", getpid());
            while (!start_task) {
                pause();
            }
            start_task = 0;

            printf("Cliente 2: Señal recibida. Abriendo %s para lectura...\n", FIFO_ENCRYPT);
            if ((fd = open(FIFO_ENCRYPT, O_RDONLY)) == -1) {
                perror("Cliente 2: open fifo_encrypt");
                exit(EXIT_FAILURE);
            }
            ssize_t bytes_read = read(fd, processed_message, BUFFER_SIZE);
            close(fd);
            if (bytes_read == -1) {
                perror("Cliente 2: read fifo_encrypt");
                exit(EXIT_FAILURE);
            }
            processed_message[bytes_read] = '\0';
            printf("Cliente 2: Cadena leída: '%s'\n", processed_message);

            if (strlen(processed_message) == 0) {
                fprintf(stderr, "Cliente 2: Cadena vacía recibida. Finalizando con error.\n");
                exit(EXIT_FAILURE);
            }

            reverse_string(processed_message);
            printf("Cliente 2: Cadena invertida: '%s'\n", processed_message);

            if ((fd = open(FIFO_DECRYPT, O_WRONLY)) == -1) {
                perror("Cliente 2: open fifo_decrypt");
                exit(EXIT_FAILURE);
            }
            if (write(fd, processed_message, strlen(processed_message) + 1) == -1) {
                perror("Cliente 2: write fifo_decrypt");
                exit(EXIT_FAILURE);
            }
            close(fd);
            printf("Cliente 2: Cadena escrita en %s. Notificando al servidor y terminando.\n", FIFO_DECRYPT);

            if (kill(getppid(), SIGUSR1) == -1) {
                perror("Cliente 2: kill SIGUSR1 to server");
            }
            exit(EXIT_SUCCESS);
        }
    }

    if (client1_pid > 0 && client2_pid > 0) { 
        printf("Servidor: Cliente 2 (PID: %d) creado. Creando Cliente 3...\n", client2_pid);
        client3_pid = fork();
        if (client3_pid < 0) {
            perror("fork client3");
            exit(EXIT_FAILURE);
        }
        if (client3_pid == 0) { // Código para Cliente 3
            struct sigaction sa_client;
            sa_client.sa_handler = client_signal_handler;
            sigemptyset(&sa_client.sa_mask);
            sa_client.sa_flags = SA_RESTART;
            if (sigaction(SIGUSR1, &sa_client, NULL) == -1) {
                perror("Cliente 3: sigaction");
                exit(EXIT_FAILURE);
            }

            printf("Cliente 3 (PID: %d): Esperando señal del servidor para iniciar...\n", getpid());
            while (!start_task) {
                pause();
            }
            start_task = 0;

            printf("Cliente 3: Señal recibida. Abriendo %s para lectura...\n", FIFO_DECRYPT);
            if ((fd = open(FIFO_DECRYPT, O_RDONLY)) == -1) {
                perror("Cliente 3: open fifo_decrypt");
                exit(EXIT_FAILURE);
            }
            ssize_t bytes_read = read(fd, processed_message, BUFFER_SIZE);
            close(fd);
            if (bytes_read == -1) {
                perror("Cliente 3: read fifo_decrypt");
                exit(EXIT_FAILURE);
            }
            processed_message[bytes_read] = '\0';
            printf("Cliente 3: Cadena leída: '%s'\n", processed_message);

            if (strlen(processed_message) == 0) {
                fprintf(stderr, "Cliente 3: Cadena vacía recibida. Finalizando con error.\n");
                exit(EXIT_FAILURE);
            }

            decrypt_string(processed_message);
            printf("Cliente 3: Cadena desencriptada: '%s'\n", processed_message);

            if ((fd = open(FIFO_RESULT, O_WRONLY)) == -1) {
                perror("Cliente 3: open fifo_result");
                exit(EXIT_FAILURE);
            }
            if (write(fd, processed_message, strlen(processed_message) + 1) == -1) {
                perror("Cliente 3: write fifo_result");
                exit(EXIT_FAILURE);
            }
            close(fd);
            printf("Cliente 3: Cadena escrita en %s. Notificando al servidor y terminando.\n", FIFO_RESULT);

            if (kill(getppid(), SIGUSR1) == -1) {
                perror("Cliente 3: kill SIGUSR1 to server");
            }
            exit(EXIT_SUCCESS);
        }
    }

    // Código del Proceso Padre (Servidor) continúa aquí
    // Solo el padre debe ejecutar esta parte
    if (client1_pid > 0 && client2_pid > 0 && client3_pid > 0) {
        printf("Servidor: Todos los clientes creados.\n");
        printf("Servidor: Enviando mensaje inicial a Cliente 1: '%s' a través de %s...\n", original_message, FIFO_MESSAGE);
        if ((fd = open(FIFO_MESSAGE, O_WRONLY)) == -1) {
            perror("Servidor: open fifo_message");
            exit(EXIT_FAILURE);
        }
        if (write(fd, original_message, strlen(original_message) + 1) == -1) {
            perror("Servidor: write fifo_message");
            exit(EXIT_FAILURE);
        }
        close(fd);
        printf("Servidor: Mensaje inicial enviado. Notificando a Cliente 1 para comenzar.\n");

        // Notificar a Cliente 1 para que comience
        if (kill(client1_pid, SIGUSR1) == -1) {
            perror("Servidor: kill SIGUSR1 to client1");
            exit(EXIT_FAILURE);
        }

        // Esperar a que Cliente 1 termine.
        printf("Servidor: Esperando que Cliente 1 termine y notifique...\n");
        while (!client1_finished) {
            pause();
        }
        printf("Servidor: Cliente 1 ha terminado. Notificando a Cliente 2 para comenzar.\n");
        // No es necesario resetear client1_finished aquí si el flujo es estrictamente secuencial y solo un cliente
        // enviará SIGUSR1 por vez al servidor en cada etapa.

        // Notificar a Cliente 2 para que comience
        if (kill(client2_pid, SIGUSR1) == -1) {
            perror("Servidor: kill SIGUSR1 to client2");
            exit(EXIT_FAILURE);
        }

        // Esperar a que Cliente 2 termine.
        printf("Servidor: Esperando que Cliente 2 termine y notifique...\n");
        while (!client2_finished) {
            pause();
        }
        printf("Servidor: Cliente 2 ha terminado. Notificando a Cliente 3 para comenzar.\n");
        // client2_finished = 0;

        // Notificar a Cliente 3 para que comience
        if (kill(client3_pid, SIGUSR1) == -1) {
            perror("Servidor: kill SIGUSR1 to client3");
            exit(EXIT_FAILURE);
        }

        // Esperar a que Cliente 3 termine.
        printf("Servidor: Esperando que Cliente 3 termine y notifique...\n");
        while (!client3_finished) {
            pause();
        }
        printf("Servidor: Cliente 3 ha terminado. Leyendo resultado final.\n");
        // client3_finished = 0;

        // El Servidor recibe el mensaje final desde fifo_result y lo imprime.
        if ((fd = open(FIFO_RESULT, O_RDONLY)) == -1) {
            perror("Servidor: open fifo_result");
            exit(EXIT_FAILURE);
        }
        ssize_t bytes_read = read(fd, processed_message, BUFFER_SIZE);
        close(fd);
        if (bytes_read == -1) {
            perror("Servidor: read fifo_result");
            exit(EXIT_FAILURE);
        }
        processed_message[bytes_read] = '\0'; // Asegurar terminación nula

        printf("Servidor: Mensaje final recibido: '%s'\n", processed_message);

        // Verifica si el mensaje final coincide con la cadena original.
        if (strcmp(original_message, processed_message) == 0) {
            printf("Servidor: El mensaje final coincide con la cadena original. Si\n");
        } else {
            printf("Servidor: El mensaje final NO coincide con la cadena original. No\n");
        }

        // Esperar a que todos los hijos terminen explícitamente para limpiar procesos zombies
        waitpid(client1_pid, NULL, 0);
        waitpid(client2_pid, NULL, 0);
        waitpid(client3_pid, NULL, 0);
        printf("Servidor: Todos los clientes han terminado.\n");

        // El Servidor elimina las tuberías con nombre al finalizar.
        printf("Servidor: Eliminando tuberías con nombre...\n");
        if (unlink(FIFO_MESSAGE) == -1) {
            perror("unlink fifo_message");
        }
        if (unlink(FIFO_ENCRYPT) == -1) {
            perror("unlink fifo_encrypt");
        }
        if (unlink(FIFO_DECRYPT) == -1) {
            perror("unlink fifo_decrypt");
        }
        if (unlink(FIFO_RESULT) == -1) {
            perror("unlink fifo_result");
        }

        printf("Servidor: Proceso finalizado.\n");
    }

    return 0;
}