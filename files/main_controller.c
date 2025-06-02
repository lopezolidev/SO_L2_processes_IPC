#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <signal.h>
#include <string.h>
#include <ctype.h> // Necesario para tolower

// Variable global para almacenar el PID (ID de Proceso) del hijo
pid_t child_pid = 0; 

// Manejador de señal para SIGINT (Ctrl+C) en el proceso padre
void handle_sigint_padre(int sig) {
    printf("\nCtrl+C recibido por el padre.\n");
    if (child_pid > 0) {
        printf("Terminando proceso hijo (PID: %d) debido a Ctrl+C...\n", child_pid);
        kill(child_pid, SIGKILL); // Envía SIGKILL para asegurar la terminación del hijo
        waitpid(child_pid, NULL, 0); // Espera a que el hijo termine para evitar procesos zombie
        printf("Proceso hijo terminado.\n");
        child_pid = 0; 
    }
    printf("Proceso padre terminando.\n");
    exit(EXIT_SUCCESS); // El padre también termina
}

int main() {
    char buffer_comando[32]; // Buffer para leer la entrada del usuario

    // Configura el manejador de señal para SIGINT (Ctrl+C)
    if (signal(SIGINT, handle_sigint_padre) == SIG_ERR) {
        perror("Error: No se pudo establecer el manejador para SIGINT");
        exit(EXIT_FAILURE);
    }

    printf("Proceso Padre (PID: %d) iniciado.\n", getpid());
    printf("Ingrese comando: 'C' (Crear), 'S' (Detener), 'G' (Continuar/Go), 'F' (Finalizar Padre e Hijo)\n");

    while (1) {
        printf("> ");
        // Leer la entrada del usuario
        if (fgets(buffer_comando, sizeof(buffer_comando), stdin) == NULL) {
            // Manejar EOF (Ctrl+D)
            printf("\nEOF detectado. Limpiando y saliendo.\n");
            if (child_pid > 0) {
                printf("Terminando proceso hijo (PID: %d)...\n", child_pid);
                kill(child_pid, SIGKILL);
                waitpid(child_pid, NULL, 0); 
            }
            break; 
        }

        buffer_comando[strcspn(buffer_comando, "\n")] = 0;

        // Procesar solo si el comando no está vacío
        if (strlen(buffer_comando) > 0) {
            char comando_char = tolower(buffer_comando[0]); 

            switch (comando_char) {
                case 'c': // Crear proceso hijo
                    if (child_pid != 0) {
                        printf("El proceso hijo ya existe (PID: %d).\n", child_pid);
                    } else {
                        child_pid = fork(); // Crear un nuevo proceso

                        if (child_pid < 0) {
                            perror("Error: fork() falló");
                            child_pid = 0; // Reiniciar en caso de fallo
                        } else if (child_pid == 0) {
                            // Este es el proceso hijo
                            printf("Proceso hijo (PID: %d) creado. Ejecutando './interface'...\n", getpid());
                            // Ejecutar el programa "interface"
                            if (execlp("./interface", "interface", (char *)NULL) == -1) {
                                perror("Hijo: Falló execlp() para ejecutar ./interface");
                                exit(EXIT_FAILURE); // Si execlp falla, el hijo debe salir
                            }
                        } else {
                            printf("Padre: Proceso hijo creado con PID: %d.\n", child_pid);
                        }
                    }
                    break;

                case 's': // Detener proceso hijo
                    if (child_pid > 0) {
                        printf("Padre: Enviando SIGSTOP al hijo (PID: %d).\n", child_pid);
                        if (kill(child_pid, SIGSTOP) == -1) {
                            perror("Error: kill(SIGSTOP) falló");
                        }
                    } else {
                        printf("No hay proceso hijo activo para detener.\n");
                    }
                    break;

                case 'g': // Continuar proceso hijo
                    if (child_pid > 0) {
                        printf("Padre: Enviando SIGCONT al hijo (PID: %d).\n", child_pid);
                        if (kill(child_pid, SIGCONT) == -1) {
                            perror("Error: kill(SIGCONT) falló");
                        }
                    } else {
                        printf("No hay proceso hijo activo para continuar.\n");
                    }
                    break;

                case 'f': // Finalizar padre e hijo
                    printf("Padre: Comando 'F' recibido. Terminando...\n");
                    if (child_pid > 0) {
                        printf("Terminando proceso hijo (PID: %d)...\n", child_pid);
                        kill(child_pid, SIGKILL); 
                        waitpid(child_pid, NULL, 0); 
                        child_pid = 0;
                    }
                    printf("Proceso padre saliendo.\n");
                    exit(EXIT_SUCCESS); // Terminar proceso padre

                default:
                    printf("Comando desconocido: '%c'. Por favor use C, S, G, o F.\n", comando_char);
                    break; // para solo utilizar los comandos especificados
            }
        }
    }

    printf("Proceso padre finalizado.\n");
    return 0;
}