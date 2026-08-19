# Algoritmo Clássico Adicional: SJF Não-Preemptivo com Estimativa

| Campo | Valor |
| --- | --- |
| Issue | ISSUE-21 ([OPCIONAL] Implementar algoritmo clássico adicional) |
| PR | PR-40 (`feat: implementar algoritmo classico adicional SJF com estimativa`) |
| Responsável | Rayan Oliveira (`@eldrayan`) |
| Revisor | Samuel Wagner (`@samsilveira`) |
| Referências | [Especificação](./especificacao.md); [Decisões](./decisoes.md); Silberschatz et al. (Operating System Concepts) |

---

## 1. Motivação e Contexto

O algoritmo **SJF (Shortest Job First)** clássico seleciona para execução o processo com a menor duração de próxima rajada de CPU. Em teoria, o SJF é comprovadamente ótimo em termos de minimização do tempo médio de espera (*turnaround*). No entanto, em sistemas reais de uso geral, a duração exata da próxima rajada de CPU **não é conhecida a priori**.

Para tornar o SJF realista e comparável academicamente sem violar a regra de não-onisciência (Decisão D10), a duração da próxima rajada é **estimada** a partir do histórico observável de rajadas anteriores do processo, utilizando a fórmula de **média móvel exponencial**:

$$\tau_{n+1} = \alpha \cdot t_n + (1 - \alpha) \cdot \tau_n$$

onde:
- $t_n$: duração real observada da última rajada de CPU concluída pelo processo (calculada como $\Delta \text{cpu\_consumed}$ na transição de bloqueio/retorno de E/S).
- $\tau_n$: estimativa anterior da rajada.
- $\tau_0$: estimativa inicial para novos processos que ainda não concluíram nenhuma rajada ($\tau_0 = 10{,}0$ ticks).
- $\alpha$: fator de ponderação histórico ($\alpha = 0{,}5$).

---

## 2. Informações Utilizadas e Disponibilidade em Sistemas Reais

O SJF implementado consome estritamente informações passadas e presentes através da estrutura de visão `SchedulerProcessView`:

1. **`pid` (`int`):** identificador único do processo.
2. **`ready_since` (`int64_t`):** instante em que o processo ingressou no estado pronto (usado como primeiro critério de desempate).
3. **`cpu_consumed` (`int64_t`):** total de CPU consumido pelo processo até o momento (utilizado para derivar $t_n = \text{cpu\_consumed} - \text{last\_cpu\_consumed}$).

### Disponibilidade em Sistemas Operacionais Reais
- **Medição de tempo de CPU:** Qualquer sistema operacional moderno (ex.: Linux via `task_struct->utime`/`stime`, contadores de hardware ou `sched_info`) registra com precisão o tempo de CPU consumido por cada thread/processo entre transições de estado.
- **Histórico e Estimativa:** Manter uma estimativa $\tau$ por processo em sua estrutura de controle (PCB / `task_struct`) requer apenas alguns bytes de memória e operações aritméticas triviais em cada retorno de E/S.
- **Ausência de Futuro:** O algoritmo **não consulta** a duração de rajadas futuras, a quantidade restante de E/S, nem o tempo de chegada de processos não admitidos.

---

## 3. Regras de Política e Desempate

O SJF opera de forma **não-preemptiva**:

1. **Seleção (`select_next`):** Seleciona o processo com o menor valor de estimativa de rajada esperada ($\tau$).
2. **Desempate:**
   - 1º Critério: Menor estimativa ($\tau$).
   - 2º Critério: Menor `ready_since` (ordem FIFO de prontidão).
   - 3º Critério: Menor `pid` (critério universal do projeto — Decisão D04).
3. **Atualização de Estimativa:**
   - Na chegada inicial (`on_arrival`): processo recebe $\tau = \tau_0 = 10{,}0$.
   - No retorno de E/S (`on_io_complete`): $t_n = \text{cpu\_consumed} - \text{last\_cpu\_consumed}$; se $t_n > 0$, atualiza $\tau \leftarrow 0{,}5 \cdot t_n + 0{,}5 \cdot \tau$.
   - Preempção (`on_preempted`): não ocorre em modo não-preemptivo (`should_preempt` retorna 0).
   - Término (`on_finish`): o estado histórico do PID é resetado.

---

## 4. Comparação e Protocolo Experimental

Para viabilizar a comparação justa sem comprometer os quatro algoritmos obrigatórios e os manifestos principais:

1. **Isolamento de Execução:** O SJF é executado através de perfis separados:
   - `make batch-sjf`: executa os 4 cenários $\times$ 100 seeds $\times$ 5 algoritmos (FCFS, RR, Prioridade, Próprio e SJF) sob o mesmo custo de troca e sementes.
   - `make batch-sjf-reduced`: executa o smoke-test reduzido de 10 execuções para validação rápida.
2. **Consistência de Carga:** Os mesmos workloads sintéticos gerados por PCG32 são compartilhados entre os 5 algoritmos, garantindo pareamento estatístico idêntico.
