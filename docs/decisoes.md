# Decisões do modelo de simulação e protocolo experimental

| Campo | Valor |
| --- | --- |
| Issue | ISSUE-01 |
| Versão | 1.0 — proposta para aprovação coletiva |
| Data | 05/08/2026 |
| Responsável | Samuel Wagner (`@samsilveira`) |
| Revisor | Sebastião Sousa (`@SebastiaoSoares`) |
| Referências | [Especificação](./especificacao.md), §§ 3–8; backlog, ISSUE-01 |
| Estado | Decisões técnicas registradas; aprovação dos sete integrantes pendente |

Este documento é o contrato comum do simulador. Motor, cargas, escalonadores e experimentos devem obedecê-lo. Uma mudança posterior que altere cargas, linha do tempo, decisões de escalonamento ou métricas invalida os resultados afetados e exige nova execução.

## Resumo executivo das decisões

| ID | Decisão |
| --- | --- |
| D01 | Simulação em tempo discreto, com um tick inteiro como unidade |
| D02 | Processos formados por rajadas alternadas, começando e terminando em CPU |
| D03 | Eventos simultâneos tratados em fases fixas; conclusão natural precede quantum |
| D04 | Menor valor numérico representa maior prioridade; desempate final por menor PID |
| D05 | Chegadas acumuladas com intervalo uniforme de 0 a 3 ticks |
| D06 | Cargas geradas por PCG32, com seed de 64 bits e carga única reutilizada pelos algoritmos |
| D07 | E/S de processos diferentes ocorre em paralelo, sem fila de dispositivo |
| D08 | Troca custa 1 tick; só conta na mudança direta entre PIDs diferentes |
| D09 | Round Robin usa quantum padrão de 4 ticks, configurável e positivo |
| D10 | Escalonadores não enxergam rajadas futuras, CPU restante ou futuras chegadas |
| D11 | Configurações em `chave=valor`; cargas, eventos e resultados em CSV; manifesto em JSON |

## 1. Modelo de tempo

### 1.1 Tempo discreto

A simulação usará **tempo discreto**. Todos os instantes e durações serão números inteiros medidos em **ticks**.

Razões para a escolha:

- facilita a inspeção manual das linhas do tempo;
- reduz a complexidade do motor e dos testes dentro do prazo do projeto;
- representa diretamente quantum e custo de troca inteiros;
- evita cancelamento ou reordenação complexa de eventos em uma fila de prioridade.

O motor pode saltar um intervalo totalmente ocioso até o próximo evento conhecido, desde que o resultado seja idêntico ao avanço tick a tick.

A implementação pode usar uma fila interna de eventos para evitar percorrer ticks ociosos. Essa otimização não altera o modelo externo: cada instante `t` deve respeitar exatamente a ordem de fases definida na seção 1.3.

### 1.2 Semântica do tick

O intervalo `[t, t + 1)` representa um tick de CPU, E/S ou troca de contexto. No início do instante `t`, o motor resolve os efeitos encerrados em `t`, atualiza as filas e decide qual atividade ocupará o intervalo seguinte.

No instante `t = 0`, não há intervalo anterior: são admitidos os processos cuja chegada vale zero e então ocorre o primeiro despacho.

### 1.3 Ordem de eventos simultâneos

Em cada instante `t`, a ordem obrigatória é:

1. Encerrar a atividade de CPU ou troca que ocupou `[t - 1, t)`.
2. Tratar o resultado da CPU, nesta precedência:
   1. finalização do processo;
   2. conclusão da rajada de CPU e início de E/S;
   3. expiração do quantum do RR;
   4. continuação normal da execução.
3. Concluir todas as operações de E/S agendadas para `t`.
4. Admitir todos os processos com chegada em `t`.
5. Reunir processos que ficaram prontos em `t` e inseri-los em ordem crescente de PID.
6. Consultar a política quando a CPU estiver livre ou quando a política puder preemptar.
7. Iniciar a troca de contexto necessária ou executar/despachar o processo escolhido.
8. Ocupar o intervalo `[t, t + 1)` com CPU, troca de contexto ou ociosidade.

Consequências importantes:

- se a rajada termina exatamente quando o quantum se esgota, ocorre **conclusão natural da rajada**, não preempção;
- um processo finalizado nunca é reinserido na fila;
- se ainda houver E/S após a rajada, o processo bloqueia antes de qualquer tratamento de quantum;
- processos que chegam ou terminam E/S no mesmo instante entram pelo mesmo critério determinístico de PID;
- a ordem não pode variar entre políticas.

### 1.4 Limites e tipos

| Campo | Regra |
| --- | --- |
| Tempo e acumuladores | Inteiro assinado de 64 bits; operações devem detectar overflow |
| PID | Inteiro de 32 bits, sequencial a partir de 1 |
| Quantidade de processos | De 1 a 100.000; experimentos principais usam 1.000 |
| Duração de CPU/E/S | De 1 a 1.000.000 ticks por rajada |
| Rajadas de CPU | De 1 a 1.000 por processo; cenários obrigatórios usam no máximo 8 |
| Tempo de chegada | Inteiro não negativo |
| Prioridade | Inteiro de 0 a 9 |
| Quantum | Inteiro de 1 a 1.000.000 ticks |
| Custo de troca | De 0 a 1.000.000; deve ser maior que zero no experimento principal |
| Seed | Inteiro sem sinal de 64 bits |

Entradas fora desses limites devem ser rejeitadas antes da simulação.

## 2. Chegada de processos e geração das cargas

### 2.1 Modelo de chegada

- O primeiro processo chega em `t = 0`.
- Para cada PID seguinte, gera-se um intervalo inteiro uniforme em `[0, 3]`.
- O tempo de chegada é a chegada anterior somada ao intervalo gerado.
- O valor zero permite pequenos lotes de chegadas simultâneas, sem concentrar toda a carga em `t = 0`.
- O mesmo modelo é usado em todos os cenários obrigatórios.

### 2.2 Gerador pseudoaleatório

Será usada uma implementação versionada do **PCG-XSH-RR 64/32 (PCG32)**. Não será usado `rand()` como fonte da carga, pois sua sequência pode variar entre implementações da biblioteca C.

Regras de reprodutibilidade:

- a seed fornecida é um inteiro de 64 bits;
- os identificadores estáveis de fluxo são `1 = equilibrado`, `2 = io_bound`, `3 = cpu_bound` e `4 = prioridades_desbalanceadas`;
- a inicialização segue a sequência fixa `state = 0`, `inc = (scenario_id << 1) | 1`, um avanço do PCG32, soma da seed ao estado e um segundo avanço;
- a ordem das chamadas ao RNG é fixa e documentada na implementação;
- a geração ocorre antes de executar qualquer escalonador;
- uma única carga é salva para cada par `cenário + seed`;
- FCFS, RR, prioridade e algoritmo próprio recebem cópias dessa mesma carga;
- nenhuma política pode chamar o gerador de carga;
- o CSV da carga recebe SHA-256, registrado no manifesto.

As seeds obrigatórias serão os inteiros de **1 a 100**, inclusive. Seeds de 101 a 1.000 ficam reservadas para eventual configuração ampliada.

### 2.3 Ordem de geração por processo

Para PIDs crescentes, o gerador consome números aleatórios nesta ordem:

1. intervalo até a chegada, exceto para PID 1;
2. classe e valor da prioridade;
3. perfil de CPU/E/S exigido pelo cenário;
4. quantidade de rajadas de CPU;
5. durações alternadas de CPU e E/S.

Alterar essa ordem muda a carga e exige incrementar a versão do esquema de workload.

## 3. Rajadas de CPU e E/S

Cada processo será representado por um vetor alternado:

```text
CPU, E/S, CPU, ..., E/S, CPU
```

Regras:

- todo processo começa e termina com CPU;
- todas as durações são inteiras e positivas;
- com `k` rajadas de CPU existem exatamente `k - 1` requisições de E/S;
- somente o motor conhece a sequência completa;
- a soma original de CPU e E/S deve ser preservada para calcular slowdown;
- uma sequência vazia, duas rajadas do mesmo tipo ou uma sequência terminando em E/S é inválida.

## 4. Parâmetros dos cenários obrigatórios

As faixas abaixo são a configuração inicial versionada. A ISSUE-11 deverá fazer um piloto quantitativo. Qualquer ajuste decorrente do piloto deve ser aprovado, versionado e aplicado novamente a todos os algoritmos antes do lote final.

### 4.1 Parâmetros comuns

- 1.000 processos por execução principal;
- chegadas conforme intervalo uniforme `[0, 3]`;
- prioridade uniforme em `[0, 9]`, exceto no cenário desbalanceado;
- todas as escolhas dentro das faixas são uniformes e inclusivas;
- todas as durações são medidas em ticks.

### 4.2 Faixas por cenário

| Cenário | Rajadas de CPU | Duração de CPU | Duração de E/S | Prioridade |
| --- | --- | --- | --- | --- |
| Equilibrado | Perfil de pouca E/S: 1–2; perfil de muita E/S: 4–7 | Perfil curto: 1–6; perfil longo: 15–40 | 5–20 | Uniforme 0–9 |
| I/O-bound | 5–8 | 1–5 | 10–30 | Uniforme 0–9 |
| CPU-bound/longos | 1–3 | 20–60 | 5–15 | Uniforme 0–9 |
| Prioridades desbalanceadas | Mesmos perfis do equilibrado | Mesmos perfis do equilibrado | Mesmos perfis do equilibrado | 80% em 0–2; 20% em 7–9 |

No cenário equilibrado, a classe curta/longa e a classe pouca/muita E/S são escolhidas independentemente com probabilidade de 50%. A classe escolhida vale para todas as rajadas daquele processo. Assim, há quatro combinações possíveis e não apenas um perfil médio.

No cenário de prioridades desbalanceadas, um sorteio uniforme em `[0, 99]` seleciona prioridade alta quando o resultado é menor que 80. Em seguida, o valor é sorteado uniformemente em `0–2` ou `7–9`, conforme a classe.

## 5. Modelo de E/S

- A E/S é solicitada imediatamente quando uma rajada de CPU termina e ainda existe outra rajada.
- O processo passa de executando para bloqueado no mesmo instante.
- A conclusão é agendada para `t_início + duração`.
- Processos diferentes podem executar E/S em paralelo sem limite de capacidade.
- Não existe dispositivo explícito nem fila de espera de dispositivo.
- Um mesmo processo não pode ter duas E/S simultâneas: enquanto bloqueado, ele não executa CPU e não pode emitir nova solicitação.
- Ao concluir a E/S, o processo entra na fila de prontos com `ready_since = t_conclusão`.
- Conclusão de E/S não interrompe FCFS nem prioridade, que são não preemptivos.
- O modelo é idêntico para todas as políticas.

Limitação a registrar no artigo: o modelo não mede contenção de dispositivos. Isso tende a reduzir a espera de processos I/O-bound e isola a comparação no escalonamento da CPU.

## 6. Prioridade e desempate

### 6.1 Convenção

**Menor número significa maior prioridade.** A faixa é de 0 a 9.

Exemplo: um processo de prioridade 1 é escolhido antes de um processo de prioridade 7 quando ambos estão prontos e a política usada é prioridade.

A prioridade é estática nos três algoritmos clássicos. O algoritmo próprio poderá calcular uma pontuação dinâmica somente com dados observáveis, sem modificar o valor original armazenado na carga.

### 6.2 Desempates

O desempate final comum é:

1. maior precedência determinada pela política;
2. menor `ready_since`, isto é, maior tempo contínuo na fila;
3. menor PID.

Aplicações:

- FCFS usa `(ready_since, PID)`;
- prioridade usa `(prioridade, ready_since, PID)`;
- RR mantém a ordem FIFO; entradas simultâneas são ordenadas por PID;
- algoritmo próprio usa sua pontuação e, em igualdade, `(ready_since, PID)`.

O retorno de E/S cria um novo `ready_since`. A ordem nunca depende de endereço de memória, ordem de iteração de tabela hash ou comportamento da biblioteca.

## 7. Troca de contexto

### 7.1 Regra de contagem e custo

O custo padrão dos experimentos principais é **1 tick**.

Uma troca é contada e cobrada quando:

- o processo `A` deixa a CPU;
- outro processo `B`, com `A != B`, é selecionado sem existir um intervalo positivo de ociosidade entre eles.

Não é troca de contexto:

- o primeiro despacho da simulação;
- a transição de processo para CPU ociosa;
- o despacho após um intervalo efetivamente ocioso;
- selecionar novamente o mesmo PID;
- o RR renovar o quantum do único processo apto.

Se `A` termina, bloqueia ou é preemptado e `B` já está pronto no mesmo instante, ocorre uma troca `A → B`.

### 7.2 Comportamento durante a troca

- a troca ocupa integralmente a CPU durante seu custo;
- nenhum processo executa CPU nesse intervalo;
- chegadas e operações de E/S continuam avançando;
- o processo de destino é escolhido e reservado no início da troca;
- eventos durante a troca não substituem o destino reservado;
- a contagem aumenta uma única vez, no início da troca;
- o mesmo custo é usado por todos os algoritmos em uma comparação.

Custo zero é permitido somente em experimentos complementares claramente identificados, nunca no lote principal.

Quando a troca de contexto termina, o processo destino reservado assume a CPU automaticamente, sem nova consulta à política de escalonamento.

## 8. Round Robin

- Quantum padrão: **4 ticks**.
- O quantum é configurável e deve ser inteiro positivo.
- O contador é reiniciado a cada despacho ou renovação sem concorrente.
- Se a rajada termina junto com o quantum, prevalece a conclusão natural.
- Se ainda resta CPU na rajada, o processo é preemptado e volta à fila com `ready_since = t`.
- Se não há outro processo pronto após reunir todos os eventos de `t`, o mesmo processo continua, recebe novo quantum e não gera troca.
- Entradas simultâneas na fila em `t` são ordenadas por PID antes da nova escolha.

Experimentos com outros valores de quantum são opcionais e não substituem o lote principal com quantum 4.

Se ainda resta CPU na rajada, o processo é preemptado e volta à fila com `ready_since = t`. A preempção só é efetivada depois de processadas todas as conclusões de E/S e chegadas do mesmo tick.

Se houver outros processos prontos, o processo preemptado é inserido após todos os processos que ficaram prontos por chegada ou conclusão de E/S naquele tick.

Se não há outro processo pronto após reunir todos os eventos de `t`, o mesmo processo continua, recebe novo quantum e não gera troca.

## 9. Informações observáveis pelos escalonadores

O motor mantém dados completos, mas entrega às políticas apenas uma visão controlada.

### 9.1 Informações permitidas

- tempo atual;
- PID;
- prioridade estática;
- tempo de chegada de processos já admitidos;
- instante de entrada atual na fila de prontos;
- tempo de espera acumulado até o instante atual;
- CPU total já consumida;
- quantidade e durações de rajadas já concluídas;
- eventos passados do próprio processo;
- ordem e membros atuais da fila de prontos;
- PID em execução e quantum já consumido, quando aplicável.

### 9.2 Informações proibidas

- processos que ainda não chegaram;
- instante ou quantidade de chegadas futuras;
- duração restante da rajada atual;
- duração ou quantidade de rajadas futuras;
- duração de futuras operações de E/S;
- tempo total de CPU originalmente gerado;
- instante futuro de conclusão;
- resultado que outra política obteria com a mesma carga.

FCFS, RR e prioridade usam apenas os campos necessários. O algoritmo próprio deve declarar cada campo utilizado e terá teste/auditoria de ausência de conhecimento futuro.

## 10. Configurações, cargas, logs e resultados

Todos os arquivos textuais usam UTF-8, fim de linha `LF`, cabeçalho quando tabulares e ponto como separador decimal.

### 10.1 Configuração

Arquivos em `configs/` usam uma linha `chave=valor`, comentários iniciados por `#` e nomes de chave em `snake_case`. Argumentos da CLI podem sobrescrever valores; os valores efetivos sempre são gravados na saída.

A precedência é obrigatoriamente `padrões internos < arquivo indicado por --config < argumentos da CLI`, independentemente da posição de `--config` na linha de comando. Chaves desconhecidas ou duplicadas e valores inválidos fazem a execução falhar antes da geração ou importação da carga. `algorithm` também pode ser informado no arquivo. Como D05 congela o modelo de chegada, `arrival_min` e `arrival_max` aceitam somente `0` e `3`.

Campos mínimos:

```text
schema_version
scenario
seed
process_count
context_switch_cost
rr_quantum
arrival_min
arrival_max
```

### 10.2 Carga de trabalho

Cada par cenário/seed produz um `workload.csv` normalizado:

```text
pid,arrival,priority,burst_index,burst_type,duration
```

`burst_type` aceita apenas `CPU` ou `IO`. O SHA-256 do arquivo identifica a carga usada por todos os algoritmos.

A representação normalizada usa cabeçalho exato, linhas em ordem crescente de PID, `burst_index` sequencial por operação, alternância iniciada e encerrada em `CPU`, inteiros decimais sem espaços e fim de linha `LF`. O hash é calculado sobre esses bytes normalizados, mesmo quando o arquivo não é persistido. Importar e exportar novamente uma carga normalizada deve produzir bytes idênticos.

`--workload-input` substitui a geração aleatória e `--workload-output` é a única forma de persistir a carga; nenhum `load.csv` é criado implicitamente. Cenário e seed permanecem metadados efetivos da execução, pois não fazem parte do CSV. A quantidade importada deve ser igual a `process_count`.

### 10.3 Log de eventos

O log detalhado é opcional e deve ser desativado no lote principal para evitar arquivos excessivos. Quando habilitado, `events.csv` usa:

```text
time,event,pid,from_state,to_state,detail
```

Eventos mínimos: `ARRIVAL`, `DISPATCH`, `CPU_BURST_END`, `PREEMPT`, `IO_START`, `IO_END`, `CONTEXT_SWITCH`, `FINISH` e `IDLE`.

### 10.4 Resultado por execução

Cada execução gera uma linha com:

```text
schema_version,run_id,workload_sha256,algorithm,scenario,seed,
process_count,context_switch_cost,rr_quantum,makespan,
mean_turnaround,context_switches,jain_slowdown_pct,status
```

Quando solicitadas, métricas individuais usam:

```text
run_id,pid,arrival,completion,turnaround,ideal_time,slowdown,
priority,total_cpu,total_io,io_requests
```

`run_id` usa apenas letras ASCII, algarismos, ponto, hífen e sublinhado. Ele é informado por `--run-id` e é obrigatório quando o resultado agregado ou individual é persistido. A saída padrão de uma execução ad hoc usa `run_id=adhoc`. Um resultado publicado após execução concluída usa `status=success`; falhas não produzem uma linha com estado de sucesso.

Métricas individuais só são persistidas mediante `--individual-output`. Todas as saídas persistentes pedidas por uma execução (workload, métricas individuais e agregado) são preparadas antes da publicação. Cada arquivo usa um temporário no mesmo diretório; a substituição dos destinos só começa após escrita, `flush` e fechamento bem-sucedidos de todos eles. Se alguma substituição falhar, os destinos anteriores são restaurados e nenhum arquivo novo da execução permanece publicado. Caminhos equivalentes, inclusive por normalização (`.` e `..`) ou links, são rejeitados quando fariam uma entrada ser sobrescrita ou duas saídas usarem o mesmo arquivo.

Definições obrigatórias:

- `turnaround = conclusão - chegada`;
- `tempo_ideal = ΣCPU + ΣE/S`;
- `slowdown = turnaround / tempo_ideal`;
- `Jain(%) = (Σslowdown)² / (n × Σslowdown²) × 100`;
- `context_switches` segue a regra da seção 7.

Métricas reais usam `double` e são exportadas com 17 algarismos significativos. A consolidação estatística não deve arredondar valores intermediários; o arredondamento é apenas de apresentação em tabelas e gráficos.

### 10.5 Manifesto experimental

`manifest.json` deve registrar:

- versão do esquema e revisão Git;
- hash do executável ou artefato de build;
- configurações e respectivos hashes;
- lista completa de seeds;
- algoritmos executados;
- quantidade de processos, custo de troca e quantum;
- comandos utilizados;
- hashes das cargas e resultados;
- horário de início/fim;
- falhas, repetições e estado de cada execução.

### 10.6 Consolidação estatística

Os resultados consolidados devem conter média e IC95% por algoritmo e cenário.

Arquivo sugerido:

`results/summary.csv`

Colunas:

`algorithm,scenario,metric,n,mean,std,ci95_low,ci95_high,unit`

Métricas mínimas consolidadas:

- mean_turnaround;
- context_switches;
- jain_slowdown_pct.

O cálculo do IC95% usa:

IC95% = mean ± 1.96 * std / sqrt(n)

onde n é o número de seeds válidas.

## 11. Parâmetros compartilhados entre algoritmos

Em cada comparação, devem ser rigorosamente iguais:

| Categoria | Parâmetros invariantes |
| --- | --- |
| Carga | Mesmo `workload.csv`, hash, cenário, seed e quantidade de processos |
| Motor | Tick, ordem de eventos, estados, regras de CPU e término |
| E/S | Mesmas durações, paralelismo, ausência de dispositivo e retorno |
| Troca | Mesma definição, custo e indisponibilidade da CPU |
| Filas | Mesmas regras de entrada e desempate final |
| Observação | Mesma barreira contra informações futuras |
| Métricas | Mesmas fórmulas e precisão numérica |
| Execução | Mesma versão do simulador e configurações efetivas |

As únicas diferenças legítimas são a regra de seleção/preempção de cada política, o quantum usado pelo RR e parâmetros explicitamente pertencentes ao algoritmo próprio. Mesmo quando não se aplicam a uma política, esses valores devem ser registrados no manifesto.

## 12. Protocolo experimental principal

- 4 cenários obrigatórios;
- 100 seeds por cenário, de 1 a 100;
- 1.000 processos por carga;
- 4 algoritmos: FCFS, RR, prioridade não preemptiva e algoritmo próprio;
- 1.600 execuções válidas;
- custo de troca igual a 1 tick;
- quantum do RR igual a 4 ticks;
- média e IC95% calculados sobre as 100 seeds de cada combinação;
- código e configurações congelados antes do lote final;
- qualquer alteração semântica posterior exige reexecução dos resultados afetados.

O piloto quantitativo deve avaliar, no mínimo:

- makespan por cenário;
- turnaround médio;
- quantidade de trocas de contexto;
- tempo de execução total;
- ociosidade da CPU;
- possíveis casos de starvation extrema.

Se o piloto indicar carga excessivamente pesada ou tempo de execução inviável, as faixas de geração devem ser ajustadas, versionadas e aplicadas novamente a todos os algoritmos antes do lote final.

## 13. Aprovação coletiva

Cada integrante deve substituir `Pendente` por `Aprovado — DD/MM/AAAA` em um commit ou comentário de PR identificável.

| Integrante | Papel na ISSUE-01 | Aprovação |
| --- | --- | --- |
| Elder Rayan (`@eldrayan`) | Colaborador | Pendente |
| Espedito Ramom (`@RamomRicarto`) | Colaborador | Pendente |
| Manoel Junio (`@Junio404`) | Colaborador | Pendente |
| Pedro Yan (`@pedropalacioo`) | Colaborador | Pendente |
| Sabrina Alencar (`@sabrinaalencar`) | Colaboradora | Pendente |
| Samuel Wagner (`@samsilveira`) | Responsável | Pendente |
| Sebastião Sousa (`@SebastiaoSoares`) | Revisor | Pendente |

### Critérios de aceite da ISSUE-01

- [x] Este documento responde ao modelo de tempo, chegada, rajadas, E/S, prioridade, trocas, RR, observabilidade e formatos.
- [x] Os parâmetros obrigatoriamente iguais entre algoritmos estão identificados.
- [x] A interface conceitual impede que políticas recebam informações futuras.
- [ ] Os sete integrantes aprovaram e registraram as decisões.

A ISSUE-01 somente pode ser encerrada depois do último item. Até lá, as decisões estão tecnicamente completas, mas ainda aguardam validação coletiva.
