# **Validação Quantitativa dos Cenários**

## **1\. Metodologia de Coleta (Piloto)**

As simulações piloto foram realizadas executando o script scripts/run\_pilot.sh.

Cada arquivo de configuração base (equilibrado, io\_bound, cpu\_bound, desbalanceado) foi rodado usando um gerador padronizado (process\_count \= 1000\) e a mesma semente seed=42. Os dados foram extraídos no formato CSV e avaliados globalmente.

## **2\. Resultados das Validações**

### **2.1. Cenário I/O-Bound**

* **Métrica-chave**: O tempo gasto total em estado de E/S devia ser mais que o dobro do tempo de CPU.  
* **Resultado**: Confirmado. O script extraiu a soma de todas as rajadas de E/S x CPU dos 1.000 processos e confirmou matematicamente a razão (Tempo E/S / Tempo CPU) \> 2.0.

### **2.2. Cenário CPU-Bound**

* **Métrica-chave**: O tempo gasto em CPU devia ser mais que o dobro do tempo de E/S.  
* **Resultado**: Confirmado. A razão (Tempo CPU / Tempo E/S) \> 2.0 foi validada pelos acúmulos contínuos do cenário.

### **2.3. Cenário de Prioridades Desbalanceadas**

* **Métrica-chave**: 70% de alta prioridade, 30% de baixa.  
* **Resultado**: A distribuição foi aplicada a faixas restritas \[0-2\] (alta) e \[7-9\] (baixa). O relatório provou uma proporção flutuando margens exatas de \~70% e \~30% devido à variância estocástica aceitável (limitada pelo rng).

### **2.4. Condição de Chegada (Sem Gargalo em t=0)**

Todos os processos tiveram uma distribuição incremental gerada aleatoriamente na seed, variando em passos de \+ \[0, 3\] ticks, inviabilizando o cenário artificial onde todos iniciam simultaneamente e provocam empilhamento não realista. O processo inicial começa em t=0 e os demais são defasados, conforme planejado.
