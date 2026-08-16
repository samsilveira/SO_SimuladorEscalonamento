# **Algoritmo Próprio: Prioridade Dinâmica Baseada em Histórico (PDBH)**

## **1. Definição do Problema e Motivação**

Nos algoritmos clássicos estudados, observamos problemas crônicos em determinados cenários:

* **FCFS:** Penaliza processos curtos e I/O-bound se processos longos (CPU-bound) chegam primeiro (efeito comboio).  
* **Prioridade Estática Não Preemptiva:** Pode causar *starvation* (inanição) de processos com baixa prioridade (valores numéricos altos) em cenários de alta carga.  
* **Round Robin:** Embora justo no uso da CPU, não diferencia a urgência (prioridade) e penaliza o *turnaround* de processos I/O-bound que não conseguem usar todo o seu *quantum* antes de bloquear.

O **PDBH (Prioridade Dinâmica Baseada em Histórico)** foi projetado para resolver o problema da inanição da prioridade estática, ao mesmo tempo em que recompensa processos que passam muito tempo na fila de prontos e penaliza processos monopolizadores de CPU.

## **2. Hipótese Mensurável**

**Hipótese:** Ao aplicar um envelhecimento (*aging*) favorável baseado no tempo de espera e uma penalização baseada no consumo histórico de CPU, o algoritmo PDBH apresentará um **Turnaround médio menor** para processos no cenário I/O-bound e um **Índice de Jain (Justiça) maior** no cenário de prioridades desbalanceadas, quando comparado ao algoritmo clássico de Prioridade não preemptiva.

**Cenários de Foco:** I/O-bound e Prioridades Desbalanceadas.

**Métricas Esperadas:** Redução no Turnaround médio (I/O-bound) e melhora no índice de Jain de Slowdown (desbalanceado).

**Custos Esperados:** Aumento no número de trocas de contexto em comparação ao FCFS, devido à natureza dinâmica da pontuação.

## **3. Informações Observáveis Utilizadas**

Para garantir a **ausência absoluta de conhecimento futuro**, o PDBH toma decisões baseadas *exclusivamente* no estado passado e presente do sistema. Os campos observáveis utilizados são:

1. static_priority: A prioridade original do processo (0 a 9).  
2. ready_since: O instante em que o processo entrou na fila de prontos atual.  
3. cpu_consumed: O tempo total de CPU já executado pelo processo desde o início da simulação.  
4. current_time: O tempo (tick) atual do motor do simulador.

**Declaração de Conformidade:** O PDBH **NÃO** utiliza: duração de rajadas futuras, tempo de chegada de processos futuros, número de rajadas restantes, ou qualquer dado que exija onisciência do simulador.

## **4. Diferenciação Funcional e Inspiração**

* **Diferença para os Clássicos:** Ao contrário da Prioridade clássica, a pontuação de prioridade do PDBH muda dinamicamente ao longo do tempo. Dois processos com a mesma prioridade estática inicial terão pontuações de despacho diferentes dependendo de quanto tempo esperaram e de quanta CPU já consumiram. Diferente do Round Robin, não é um algoritmo baseado em *fatias de tempo* (*quantum*).  
* **Inspiração Externa:** O algoritmo é fortemente inspirado no conceito de *Aging* (Envelhecimento) da literatura de SO (Silberschatz, Galvin & Gagne, *Operating System Concepts*), e no mecanismo de penalidade por uso de CPU presente em filas de múltiplo nível com realimentação (MLFQ), porém condensado em uma única fórmula matemática não preemptiva para se adequar ao contrato do motor.

## **5. Pseudocódigo e Regra de Decisão**

A regra de decisão consiste em calcular um dynamic_score para cada processo na fila de prontos sempre que a CPU estiver livre. **Lembrando a regra do projeto: Menor número significa maior prioridade.**

**Constantes do Algoritmo:**

* AGING_FACTOR = 10 (A cada 10 ticks esperando, a prioridade melhora em 1).  
* PENALTY_FACTOR = 5 (A cada 5 ticks de CPU consumidos globalmente, a prioridade piora em 1).

```
FUNÇÃO EscolherProximoProcesso(fila_prontos, current_time):  
    melhor_processo = NULO  
    melhor_score = INFINITO  
      
    PARA CADA processo NA fila_prontos:  
        espera_atual = current_time - processo.ready_since  
          
        // Cálculo da pontuação dinâmica (Matemática com inteiros)  
        bonus_espera = espera_atual / 10  
        penalidade_cpu = processo.cpu_consumed / 5  
          
        dynamic_score = processo.static_priority - bonus_espera + penalidade_cpu  
          
        // Aplicação do desempate obrigatório da Especificação  
        SE dynamic_score < melhor_score:  
            melhor_score = dynamic_score  
            melhor_processo = processo  
              
        SENÃO SE dynamic_score == melhor_score:  
            // Regra de desempate final: (ready_since, PID)  
            SE processo.ready_since < melhor_processo.ready_since:  
                melhor_processo = processo  
            SENÃO SE processo.ready_since == melhor_processo.ready_since E processo.pid < melhor_processo.pid:  
                melhor_processo = processo  
                  
    RETORNAR melhor_processo
```

*(Nota: a divisão de inteiros em C truncará os decimais, o que é o comportamento desejado para criar "degraus" de pontuação).*

## **6. Exemplo Passo a Passo**

**Cenário:** Tempo atual (current_time) = 50. A CPU acaba de ficar livre.

Temos 2 processos na fila de prontos:

* **P1:** static_priority = 2, ready_since = 20, cpu_consumed = 15.  
* **P2:** static_priority = 0 (mais alta), ready_since = 45, cpu_consumed = 30.

**Cálculo da decisão:**

* **P1:** * espera = 50 - 20 = 30 ticks.  
  * bonus_espera = 30 / 10 = 3.  
  * penalidade_cpu = 15 / 5 = 3.  
  * score_P1 = 2 - 3 + 3 = **2**.  
* **P2:**  
  * espera = 50 - 45 = 5 ticks.  
  * bonus_espera = 5 / 10 = 0.  
  * penalidade_cpu = 30 / 5 = 6.  
  * score_P2 = 0 - 0 + 6 = **6**.

**Decisão:** O PDBH escolhe **P1** (score 2 < score 6).

*Justificativa do exemplo:* Apesar de P2 ter uma prioridade estática melhor (0), ele consumiu muita CPU no passado e acabou de entrar na fila. P1 estava esperando há muito tempo, o mecanismo de *aging* fez sua prioridade vencer a penalidade de CPU e superar P2.

## **7. Custos e Limitações**

* **Custo de Processamento (Overhead):** Diferente do FCFS, onde basta tirar o primeiro da fila (complexidade *O(1)*), o PDBH exige varrer toda a fila de prontos calculando a fórmula para cada processo a cada despacho, gerando complexidade *O(N)*.  
* **Limitação:** Não sendo preemptivo, o algoritmo não pode arrancar um processo longo da CPU imediatamente caso a pontuação de outro na fila se torne melhor durante a execução da rajada; ele só age nos pontos de decisão (conclusão natural de rajada, I/O, etc).
