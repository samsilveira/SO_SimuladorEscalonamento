# Algoritmo Próprio: Prioridade Dinâmica Baseada em Histórico (PDBH)

---

## Motivação

Nos algoritmos clássicos estudados, observamos problemas crônicos em determinados cenários:

- **FCFS:** penaliza processos curtos e I/O-bound quando processos longos (CPU-bound) chegam antes (efeito comboio).
- **Prioridade Estática Não Preemptiva:** pode causar *starvation* (inanição) de processos com baixa prioridade (valores numéricos altos) em cenários de alta carga.
- **Round Robin:** é justo no uso de CPU, mas não diferencia urgência (prioridade) e penaliza o *turnaround* de processos I/O-bound que não usam todo o *quantum* antes de bloquear.

O **PDBH (Prioridade Dinâmica Baseada em Histórico)** foi projetado para **reduzir o risco** de inanição da prioridade estática — não para eliminá-lo matematicamente (ver [Limitações conhecidas](#limitações-conhecidas)) —, ao mesmo tempo em que recompensa processos que esperam muito tempo na fila de prontos e penaliza processos que já monopolizaram bastante CPU no passado.

---

## Hipótese mensurável

**Hipótese:** ao aplicar um envelhecimento (*aging*) favorável baseado no tempo de espera e uma penalização baseada no consumo **individual** e histórico de CPU de cada processo, o PDBH apresentará um **turnaround médio menor** no cenário I/O-bound e um **índice de Jain do slowdown maior** no cenário de prioridades desbalanceadas, quando comparado ao algoritmo clássico de Prioridade não preemptiva.

**Cenários de foco:** I/O-bound e prioridades desbalanceadas (cenários 2 e 4 da especificação, §6).

**Métricas esperadas de melhora:**
- Redução no turnaround médio (cenário I/O-bound).
- Melhora no índice de Jain do slowdown (cenário de prioridades desbalanceadas).

**Métrica a ser observada, sem previsão de sinal (não é um custo assumido a priori):**
- **Trocas de contexto.** O PDBH **não é preemptivo**: os pontos de decisão de despacho ocorrem exatamente nos mesmos instantes que em FCFS e Prioridade estática — conclusão natural de rajada, bloqueio para E/S, retorno de E/S para uma fila vazia. A reordenação dinâmica muda **qual** processo é escolhido em cada despacho, não **quantos** despachos ocorrem. Por isso, não há garantia teórica de que o número de trocas de contexto aumente em relação ao FCFS ou à Prioridade estática; pode haver efeito indireto (mudanças na ordem de execução alteram quando processos chegam a uma fila vazia), mas isso será tratado como resultado experimental, não como custo assumido na hipótese.

---

## Informações observáveis utilizadas

Para garantir a **ausência absoluta de conhecimento futuro**, o PDBH toma decisões baseado *exclusivamente* no estado passado e presente do sistema:

1. `static_priority` (`int`): prioridade original do processo (0 a 9).
2. `ready_since` (`int64_t`): instante em que o processo entrou na fila de prontos **na entrada atual** (reinicia sempre que o processo retorna de E/S ou é reenfileirado).
3. `cpu_consumed` (`int64_t`): tempo total de CPU já executado **por este processo específico**, acumulado desde o início da simulação — **nunca reinicia**, mesmo após passagens por E/S.
4. `current_time` (`int64_t`): tick atual do motor do simulador, fornecido a cada chamada de despacho.

Todos os campos temporais e acumuladores (`ready_since`, `cpu_consumed`, `current_time`, e o `dynamic_score` derivado deles) usam **`int64_t`**, na mesma convenção de tipos já adotada pelo motor para tempo e acumuladores (`docs/decisoes.md`, seção 1.4) — evita overflow silencioso em cargas de 1.000+ processos com rajadas de até 1.000.000 ticks e mantém o tipo consistente entre a visão do escalonador e o `Process` interno do motor.

**Esclarecimento sobre o escopo de `cpu_consumed` (correção de revisão):** este valor é estritamente **individual e cumulativo por processo**, não uma métrica agregada do sistema. Se fosse um contador global de CPU consumida por todos os processos, a penalidade seria idêntica para qualquer processo avaliado no mesmo instante, anulando seu papel de diferenciar processos CPU-bound de processos que pouco usaram a CPU. É exatamente por ser individual que o mecanismo penaliza quem historicamente monopolizou o processador.

**Declaração de conformidade:** o PDBH **NÃO** utiliza duração de rajadas futuras, tempo de chegada de processos futuros, número de rajadas restantes, tempo de CPU/E-S restante de qualquer processo, ou qualquer dado que exija onisciência do simulador.

Auditoria explícita (também verificada em testes, ver [Plano de testes](#plano-de-testes)):

- [ ] Duração total da próxima rajada de CPU de processos ainda não chegados — **não utilizada**.
- [ ] Número de rajadas restantes de qualquer processo — **não utilizado**.
- [ ] Tempo de chegada de processos futuros — **não utilizado**.
- [ ] CPU restante ou rajadas futuras do próprio processo — **não utilizado**.

---

## Extensão de contrato adotada (motor ↔ escalonador)

O projeto já define, em `include/scheduler.h`, uma visão somente leitura (`SchedulerProcessView`) que o motor entrega a qualquer política — nenhuma política, incluindo o PDBH, recebe `Process *` nem qualquer ponteiro para o estado interno do motor. A seleção também **não retorna um processo ou uma view**: retorna um código de resultado (`SchedulerSelectResult`) e o **PID** escolhido, via parâmetro de saída — o mesmo contrato já usado por FCFS, RR e Prioridade. A extensão exigida pelo PDBH preserva integralmente esse contrato, apenas acrescentando os dois campos que faltavam:

- `cpu_consumed` na `SchedulerProcessView` (o motor já expunha `pid`, `priority`, `arrival` e `ready_since`).
- `current_time` como parâmetro explícito de `scheduler_select_next`, para permitir o recálculo dinâmico da pontuação no instante do despacho.

```c
// include/scheduler.h — visão somente leitura já usada por todas as políticas.
typedef struct {
    int pid;
    int priority;          // static_priority (0 a 9)
    int64_t arrival;
    int64_t ready_since;   // reinicia sempre que o processo reentra na fila de prontos
    int64_t cpu_consumed;  // cumulativo, individual, nunca reinicia   <- campo adicionado
} SchedulerProcessView;

// Assinatura de seleção: recebe o tempo atual e devolve o PID escolhido por
// parâmetro de saída. Nenhuma política recebe Process* nem SchedulerProcessView*
// de volta — apenas o inteiro pid, como já ocorria antes da extensão.
SchedulerSelectResult scheduler_select_next(Scheduler *scheduler,
                                            int64_t current_time,   // <- parâmetro adicionado
                                            int *pid);
```

O motor continua responsável por popular `SchedulerProcessView` a partir do `Process` interno (incluindo o reset de `ready_since` no retorno de E/S e a atualização de `cpu_consumed` a cada tick de CPU efetivamente executado) e por repassar `current_time` a cada chamada de seleção. FCFS, RR e Prioridade recebem o mesmo `current_time` no parâmetro, mas o ignoram — apenas o PDBH o utiliza. A extensão é a mesma para todos os algoritmos avaliados, mantendo a comparação justa (especificação §4).

---

## Pseudocódigo

A regra de decisão calcula um `dynamic_score` para cada processo na fila de prontos sempre que a CPU estiver livre. **Regra do projeto: menor número significa maior prioridade.**

**Constantes do algoritmo:**

- `AGING_FACTOR = 10` — a cada 10 ticks esperando, a prioridade melhora em 1.
- `PENALTY_FACTOR = 5` — a cada 5 ticks de CPU consumidos **pelo próprio processo**, a prioridade piora em 1.

**Por que 10 e 5, e não outros valores?** A escolha estabelece deliberadamente uma relação 2:1 entre penalidade e envelhecimento: um processo perde uma "posição" de prioridade duas vezes mais rápido pelo uso de CPU do que ganha por esperar. Isso favorece processos curtos/I/O-bound (que não acumulam `cpu_consumed`) sem fazer com que o envelhecimento sozinho anule rapidamente qualquer prioridade estática ruim — um processo de baixa prioridade estática ainda precisa esperar um tempo proporcional para ultrapassar concorrentes com prioridade estática melhor. Os valores foram escolhidos de forma exploratória (não empírica ainda) para produzir "degraus" de pontuação na mesma ordem de grandeza das durações de rajada típicas geradas nos cenários da especificação.

**Protocolo de calibração:** `AGING_FACTOR` e `PENALTY_FACTOR` devem ser **congelados antes do lote de experimentos final** (mesma exigência de congelamento de código/configuração do protocolo experimental, especificação §12). Caso a equipe decida recalibrar os fatores a partir de resultados empíricos, essa calibração deve usar um conjunto de seeds **separado e não sobreposto** ao conjunto de seeds usado na avaliação final (por exemplo, seeds de calibração fora da faixa 1–100 reservada às execuções principais) — do contrário, os fatores estariam ajustados aos mesmos dados usados para medir o desempenho do algoritmo, inflando artificialmente os resultados reportados (vazamento de dados/overfitting experimental). Qualquer alteração dos fatores após o congelamento invalida os resultados obtidos até então e exige nova execução do lote afetado, na mesma lógica já aplicada a mudanças de carga ou de motor (`docs/decisoes.md`).

**O score pode ficar negativo — sim, e isso é esperado.** Como `bonus_espera` cresce sem limite superior enquanto `static_priority` e `penalidade_cpu` não têm limite inferior conjunto que o compense, um processo que espera muito tempo e consumiu pouca CPU pode facilmente produzir `dynamic_score < 0`. Isso é intencional: o algoritmo compara valores relativos (quanto menor, mais prioritário), então um score negativo apenas expressa "prioridade efetiva muito alta" e não exige nenhum tratamento especial (sem *clamping*, sem valor mínimo artificial) — a implementação deve usar um tipo inteiro com sinal para `dynamic_score` e as comparações continuam corretas. Um exemplo numérico está na seção de exemplo, abaixo.

```
// Tipos: current_time, ready_since, cpu_consumed e dynamic_score são int64_t
// (com sinal); static_priority e pid permanecem int, como no restante do motor.
FUNÇÃO EscolherProximoProcesso(fila_prontos, current_time: int64_t):
    melhor_processo = NULO
    melhor_score: int64_t = INFINITO

    PARA CADA processo NA fila_prontos:
        espera_atual: int64_t = current_time - processo.ready_since

        // Cálculo da pontuação dinâmica (matemática com inteiros, com truncamento)
        bonus_espera: int64_t = espera_atual / AGING_FACTOR
        penalidade_cpu: int64_t = processo.cpu_consumed / PENALTY_FACTOR

        dynamic_score: int64_t = processo.static_priority - bonus_espera + penalidade_cpu

        SE dynamic_score < melhor_score:
            melhor_score = dynamic_score
            melhor_processo = processo

        SENÃO SE dynamic_score == melhor_score:
            // Regra de desempate obrigatória: (ready_since, depois PID)
            SE processo.ready_since < melhor_processo.ready_since:
                melhor_processo = processo
            SENÃO SE processo.ready_since == melhor_processo.ready_since E processo.pid < melhor_processo.pid:
                melhor_processo = processo

    RETORNAR melhor_processo
```

*(Nota: a divisão de inteiros trunca os decimais — comportamento desejado para criar "degraus" discretos de pontuação, evidenciado no exemplo abaixo.)*

---

## Exemplo passo a passo (trace manual com 3+ processos)

**Processos:**

| Processo | pid | static_priority | chega em |
| --- | --- | --- | --- |
| P1 | 1 | 2 | t = 0 |
| P2 | 2 | 1 | t = 3 |
| P3 | 3 | 4 | t = 6 |

**Estrutura de rajadas:**
- P1: rajada CPU de 20 ticks → E/S de 15 ticks → rajada final de CPU de 9 ticks.
- P2: rajada única de 15 ticks (sem E/S).
- P3: aguarda até ser despachado; depois rajada final de 6 ticks.

**Custo de troca de contexto:** este trace usa o custo padrão dos experimentos principais, **1 tick** (`docs/decisoes.md`, D08), contado sempre que a CPU passa de um PID para outro sem intervalo ocioso entre eles — o mesmo custo aplicado a todos os algoritmos. O primeiro despacho da simulação não gera troca. A **pontuação é sempre calculada no instante em que a CPU fica livre** (quando `scheduler_select_next` é chamada), e só depois o despacho efetivo é adiado pelo custo de troca — é assim que o motor já funciona (`src/simulation.c`); é esse instante, anterior ao atraso da troca, o valor de `current_time` usado abaixo.

### Decisão 1 — t = 0 (chegada de P1)

Fila = {P1}. Único processo pronto → despacho trivial de **P1**, sem custo de troca (primeiro despacho da simulação). P1 executa de t=0 a t=20.
Durante esse intervalo: P2 chega em t=3 (`ready_since=3`); P3 chega em t=6 (`ready_since=6`).

### Decisão 2 — t = 20 (P1 bloqueia para E/S)

P1 solicita E/S (retorna em t=35). A seleção ocorre em `current_time=20`. Fila = {P2, P3}.

- **P2:** espera = 20−3 = 17 → `bonus_espera` = 17/10 = **1**; `cpu_consumed`=0 → penalidade=0. `score = 1 − 1 + 0 = 0`.
- **P3:** espera = 20−6 = 14 → `bonus_espera` = 14/10 = **1**; `cpu_consumed`=0 → penalidade=0. `score = 4 − 1 + 0 = 3`.

**Seleciona P2** (0 < 3). Como P1 (pid 1) deixa a CPU e P2 (pid 2) é diferente, há troca de contexto: 1 tick de custo, então **P2 passa a executar de t=21 a t=36** (15 ticks), terminando em t=36.

### Decisão 3 — t = 36 (fim de P2; P1 já havia retornado da E/S)

P1 retorna da E/S em t=35 (enquanto P2 ainda executava) — `ready_since` de P1 é **reiniciado** para 35, mas `cpu_consumed` permanece em 20 (não reinicia). P1 apenas entra na fila nesse instante; a CPU só fica livre em t=36, quando P2 termina, e é nesse instante que a próxima seleção ocorre (`current_time=36`). P3 nunca foi despachado; seu `ready_since` continua em 6. Fila = {P1, P3}.

- **P1:** espera = 36−35 = 1 → bonus = 1/10 = **0**; `cpu_consumed`=20 → penalidade = 20/5 = **4**. `score = 2 − 0 + 4 = 6`.
- **P3:** espera = 36−6 = 30 → bonus = 30/10 = **3**; `cpu_consumed`=0 → penalidade=0. `score = 4 − 3 + 0 = 1`.

**Seleciona P3** (1 < 6).

**Ponto de divergência funcional:** a Prioridade estática clássica, olhando apenas `static_priority`, compararia P1 (2) contra P3 (4) e escolheria **P1** (menor valor = melhor). O PDBH escolhe **P3**, porque o histórico de CPU de P1 (20 ticks já consumidos) pesou mais que sua prioridade estática melhor. Isso demonstra ordem de execução diferente da Prioridade clássica no mesmo estado de fila — o critério da especificação de "diferença funcional real".

Troca de contexto (P2 → P3, PIDs diferentes): 1 tick de custo. **P3 executa de t=37 a t=43** (rajada final de 6 ticks) e termina em t=43.

### Decisão 4 — t = 43 (único processo restante)

Fila = {P1} (`ready_since=35`, `cpu_consumed=20`, inalterados desde a decisão 3, pois P1 não foi despachado nem se moveu). Seleção trivial de **P1** em `current_time=43` (espera=8→bonus=0; penalidade=4; `score=6`, único candidato). Troca de contexto (P3 → P1): 1 tick de custo. **P1 executa de t=44 a t=53** (rajada final de 9 ticks) e termina em t=53. Simulação deste trace encerrada.

### Exemplo complementar — score negativo

Considere um processo hipotético P9, nunca executado, avaliado num ponto de decisão isolado: `static_priority=1`, `ready_since=0`, `cpu_consumed=0`, `current_time=45`.

`espera = 45 → bonus_espera = 45/10 = 4`; `penalidade_cpu = 0`.
`dynamic_score = 1 − 4 + 0 = −3`.

O score negativo é válido e não recebe nenhum tratamento especial: apenas indica prioridade efetiva muito alta, e a comparação com outros processos continua correta desde que `dynamic_score` seja um inteiro com sinal.

### Exemplo complementar — desempate

**Caso A (desempate por `ready_since`):** dois processos, mesmo `dynamic_score` calculado (por exemplo, ambos = 4), mas `ready_since` diferentes: P_A com `ready_since=48` e P_B com `ready_since=52`. Como 48 < 52, **P_A vence** sem precisar comparar PID.

**Caso B (cascata completa até PID):** P6 (`pid=6`, `static_priority=5`, `ready_since=50`, `cpu_consumed=0`) e P7 (`pid=7`, `static_priority=5`, `ready_since=50`, `cpu_consumed=0`), avaliados em `current_time=60`.

Ambos: espera=10 → bonus=1; penalidade=0. `score = 5 − 1 + 0 = 4` para os dois — **empate no score**. `ready_since` também empatado (50 = 50). Critério final: `pid`: 6 < 7 → **P6 vence**.

---

## Diferenças funcionais em relação a FCFS, RR e Prioridade

- **Vs. FCFS:** FCFS ignora completamente prioridade e histórico de CPU, despachando estritamente por ordem de chegada à fila. O PDBH usa quatro sinais (`static_priority`, tempo de espera, CPU consumida, tempo atual) para recalcular uma pontuação a cada despacho — a ordem de chegada só importa como critério de desempate secundário. A Decisão 2 do exemplo já mostra o PDBH preferindo P2 a P3 por razões de prioridade/espera que o FCFS nem consideraria.
- **Vs. Round Robin:** RR não diferencia urgência/prioridade — todos os processos giram pelo mesmo *quantum*. O PDBH não usa fatias de tempo; despacha um processo por completo até seu próximo ponto de decisão natural (fim de rajada, bloqueio de E/S), escolhendo por pontuação, não por rodízio.
- **Vs. Prioridade não preemptiva:** a Prioridade clássica usa apenas `static_priority`, fixo durante toda a simulação — sujeito a inanição de processos de baixa prioridade. O PDBH recalcula a pontuação a cada despacho a partir do histórico observado (envelhecimento reduz o score de quem espera muito; penalidade aumenta o score de quem já consumiu muita CPU), produzindo ordens de despacho diferentes da Prioridade clássica em situações concretas — demonstrado explicitamente na Decisão 3 do exemplo acima, onde P3 (prioridade estática pior) é escolhido sobre P1 (prioridade estática melhor, porém com histórico pesado de CPU).

---

## Limitações conhecidas

- **Overhead de processamento:** ao contrário do FCFS (O(1), basta remover o primeiro da fila), o PDBH exige varrer toda a fila de prontos e recalcular a fórmula a cada despacho — complexidade **O(N)**.
- **Ausência de preempção:** o PDBH não interrompe uma rajada em execução, mesmo que a pontuação de outro processo na fila se torne melhor durante essa execução; ele só age nos pontos de decisão naturais (fim de rajada, bloqueio para E/S, retorno de E/S para fila vazia). Por isso, **o número de trocas de contexto não é uma consequência garantida do algoritmo** — é uma métrica a ser medida experimentalmente, e não um custo assumido a priori (ver Hipótese).
- **Assimetria entre `cpu_consumed` (cumulativo) e `ready_since` (reiniciado a cada retorno de E/S):** `cpu_consumed` nunca decai — soma-se durante toda a vida do processo — enquanto `ready_since` é zerado sempre que o processo volta à fila após E/S. Isso significa que, para processos CPU-bound que passam por vários ciclos CPU→E/S→CPU ao longo da simulação, a penalidade acumulada de CPU persiste integralmente, mas o bônus de envelhecimento "reinicia do zero" a cada retorno de E/S. Em cargas com muitas interações E/S ao longo do tempo, esse desenho pode penalizar desproporcionalmente processos que já executaram bastante CPU no passado, mesmo que atualmente estejam esperando há relativamente pouco tempo desde seu último retorno — um viés estrutural contra processos CPU-bound de vida longa que deve ser observado nos resultados experimentais (especialmente no cenário CPU-bound/processos longos, especificação §6.3) e discutido no artigo. Mitigações possíveis (fora do escopo desta entrega) incluiriam decaimento de `cpu_consumed` ao longo do tempo ou normalização da penalidade pela idade do processo.
- **Mitigação de starvation, não eliminação:** o PDBH **reduz o risco** de inanição em relação à Prioridade estática, mas não oferece garantia matemática de limite superior de espera. O risco cresce quando **`PENALTY_FACTOR` é pequeno em relação a `AGING_FACTOR`** (penalidade muito agressiva por CPU consumida): um processo que executou uma quantidade razoável de CPU acumula uma penalidade alta e permanente (`cpu_consumed` nunca decai), e passa a precisar de um tempo de espera proporcionalmente maior — a cada retorno à fila, com `ready_since` reiniciado do zero — só para que seu envelhecimento volte a compensar essa penalidade. Nesse regime, processos que já executaram CPU no passado ficam em desvantagem persistente frente a novos processos que acabam de chegar com boa prioridade estática e `cpu_consumed=0`. Chegada contínua de tais processos ainda pode, em teoria, manter um processo específico em desvantagem por longos períodos, mesmo sem starvation permanente (o envelhecimento é ilimitado no tempo, então a vantagem dos recém-chegados eventualmente é superada, mas o atraso pode ser arbitrariamente grande). Fatores extremos nessa direção devem ser evitados na calibração (ver Pseudocódigo).
- **Sensibilidade aos fatores:** `AGING_FACTOR` e `PENALTY_FACTOR` foram escolhidos de forma exploratória (ver justificativa na seção de Pseudocódigo); o comportamento do algoritmo é sensível a esses valores. Qualquer calibração adicional deve seguir o protocolo descrito na seção de Pseudocódigo (fatores congelados antes do lote final, ou calibrados com seeds separadas das usadas na avaliação).

---

## Plano de testes

Cobertura mínima exigida (critérios de aceite + observações de revisão):

1. **Seleção básica:** fila com um único processo pronto (despacho trivial); fila vazia (CPU permanece ociosa).
2. **Empates — cascata completa:**
   - Mesmo `dynamic_score`, `ready_since` diferentes → vence o menor `ready_since` (Caso A do exemplo).
   - Mesmo `dynamic_score` e mesmo `ready_since`, PIDs diferentes → vence o menor PID (Caso B do exemplo).
3. **Retorno de E/S:** verificar que, ao processo retornar da E/S, `ready_since` é reiniciado para o tick de retorno e `cpu_consumed` **permanece inalterado** (não reinicia).
4. **Ausência de preempção:** com um processo em execução, inserir na fila um processo cujo `dynamic_score` seria melhor no meio da rajada em curso; verificar que **não há troca** até o próximo ponto de decisão natural.
5. **Limites dos fatores (`AGING_FACTOR`/`PENALTY_FACTOR`):**
   - Fronteira de truncamento: espera = `AGING_FACTOR − 1` deve gerar `bonus_espera = 0`; espera = `AGING_FACTOR` deve gerar `bonus_espera = 1` (mesma lógica para `PENALTY_FACTOR`).
   - Fatores extremos: `PENALTY_FACTOR` muito menor que `AGING_FACTOR` (e vice-versa) não deve causar erro ou overflow, apenas mudança de comportamento relativo.
6. **Valores extremos:**
   - `static_priority` nos limites (0 e 9).
   - `cpu_consumed` muito grande (processo de vida longa) — verificar ausência de overflow no tipo usado e que o `dynamic_score` resultante, mesmo grande, é tratado corretamente na comparação.
   - Espera muito longa produzindo `dynamic_score` negativo — verificar que a comparação permanece correta com inteiro com sinal, sem *clamping*.
   - Múltiplos processos com `static_priority`, `ready_since` e `cpu_consumed` idênticos, diferindo **apenas no `pid`** (PIDs nunca podem ser idênticos entre processos) — situação de empate total no `dynamic_score` e no `ready_since`, resolvida exclusivamente pelo terceiro nível do desempate (menor PID).
7. **Auditoria de ausência de conhecimento futuro:** teste de contrato/assinatura garantindo que a seleção do PDBH só recebe `SchedulerProcessView` e `current_time` (nunca `Process *`) e que `SchedulerProcessView` não contém duração de rajada futura, número de rajadas restantes, tempo de chegada de outros processos ou qualquer dado equivalente — reforça a lista de verificação da seção de Informações observáveis.

---

## Referências de inspiração

- **Envelhecimento (*aging*):** Silberschatz, A.; Galvin, P. B.; Gagne, G. *Operating System Concepts*. Conceito clássico usado para evitar inanição em escalonadores baseados em prioridade, ajustando a prioridade efetiva em função do tempo de espera.
- **Penalidade por uso de CPU:** mecanismo inspirado nas filas de múltiplo nível com realimentação (*Multilevel Feedback Queue* — MLFQ), nas quais processos que consomem mais CPU são rebaixados para níveis de menor prioridade. O PDBH condensa essa ideia em uma única fórmula matemática não preemptiva, sem múltiplas filas físicas, para se adequar ao contrato do motor de simulação do projeto.