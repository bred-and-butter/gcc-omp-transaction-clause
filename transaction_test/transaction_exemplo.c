
#include <stdio.h>
#include <omp.h>

#define N 1000

int main(void)
{
    int vetor[N];
    int a;

    // Inicialização sequencial
    for (int i = 0; i < N; i++)
        vetor[i] = (i * 37 + 19) % 997;

    int maior = vetor[0];
    int posicao = 0;

    #pragma omp parallel
    {
	int b;
        int maior_local = -1;
        int posicao_local = -1;

        // Cada thread procura o maior valor
        // entre os elementos que lhe foram atribuídos.
        #pragma omp for
        for (int i = 0; i < N; i++) {
            if (vetor[i] > maior_local) {
                maior_local = vetor[i];
                posicao_local = i;
            }
        }

        // Atualização do resultado compartilhado.
        #pragma omp transaction( a, b, c)
        {
            if (maior_local > maior) {
                maior = maior_local;
                posicao = posicao_local;
            }
        }
    }

    printf("Maior valor: %d\n\n", maior);
    printf("Posicao: %d\n\n", posicao);

    return 0;
}

