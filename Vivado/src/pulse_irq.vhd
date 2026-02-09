library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity pulse_irq is
    Port ( 
        clk      : in  STD_LOGIC;  -- 100 MHz
        reset_n  : in  STD_LOGIC;
        irq_out  : out STD_LOGIC_VECTOR(5 downto 0);
        led_out  : out STD_LOGIC
    );
end pulse_irq;

architecture Behavioral of pulse_irq is
    -- Constantes de tiempo (Ciclos a 100MHz)
    constant MAX_10MS_P  : integer := 1000000; -- ~10 ms   (1000 samples)
    constant MAX_10MS    : integer := 1066667; -- ~10.6 ms (1024 samples)
    constant MAX_DAVE    : integer := 66667;   -- 0.667 ms (64 samples)
    constant MAX_1MS     : integer := 16667;   -- 0.166 ms (16 samples)
    constant MAX_STRESS  : integer := 1042;    -- 0.010 ms (1 sample @ 96kHz)

    constant MAX_BLINK   : integer := 50000000;

    -- Counters individuales
    signal cnt_10ms_p, cnt_10ms, cnt_1ms, cnt_dave, cnt_stress : integer range 0 to 1066667 := 0;
    signal blink_counter : integer range 0 to MAX_BLINK := 0;
    
    -- Registros de pulso y hold individuales (para conservar tu logica original)
    signal reg_10ms_p, reg_10ms, reg_1ms, reg_dave, reg_stress : std_logic := '0';
    signal hold_10ms_p, hold_10ms, hold_1ms, hold_dave, hold_stress : integer range 0 to 15 := 0;
    
    signal led_state : std_logic := '0';
begin

    process(clk, reset_n)
    begin
        if reset_n = '0' then
            cnt_10ms_p <= 0;
            cnt_10ms   <= 0;
            cnt_1ms    <= 0; 
            cnt_dave   <= 0; 
            cnt_stress <= 0;

            reg_10ms_p <= '0';
            reg_10ms   <= '0'; 
            reg_1ms    <= '0'; 
            reg_dave   <= '0'; 
            reg_stress <= '0';
            
            hold_10ms_p <= 0; 
            hold_10ms   <= 0; 
            hold_1ms    <= 0; 
            hold_dave   <= 0; 
            hold_stress <= 0;

            blink_counter <= 0; 
            led_state     <= '0';
        elsif rising_edge(clk) then
            
            -- --- LOGICA DE CANALES (Tu estructura original repetida) ---

            -- Canal 5: 10ms_p (Baseline)
            if cnt_10ms_p < (MAX_10MS_P - 1) then 
                cnt_10ms_p <= cnt_10ms_p + 1;
            else 
                cnt_10ms_p <= 0; 
                hold_10ms_p <= 10; 
            end if;

            -- Canal 0: 10ms (Baseline)
            if cnt_10ms < (MAX_10MS - 1) then 
                cnt_10ms <= cnt_10ms + 1;
            else 
                cnt_10ms <= 0; 
                hold_10ms <= 10; 
            end if;
            
            -- Canal 1: 1ms
            if cnt_1ms < (MAX_1MS - 1) then 
                cnt_1ms <= cnt_1ms + 1;
            else 
                cnt_1ms <= 0; 
                hold_1ms <= 10; 
            end if;

            -- Canal 2: 0.667ms (Target de Dave)
            if cnt_dave < (MAX_DAVE - 1) then 
                cnt_dave <= cnt_dave + 1;
            else 
                cnt_dave <= 0; 
                hold_dave <= 10; 
            end if;

            -- Canal 3: 0.1ms (Stress)
            if cnt_stress < (MAX_STRESS - 1) then 
                cnt_stress <= cnt_stress + 1;
            else 
                cnt_stress <= 0; 
                hold_stress <= 10; 
            end if;

            -- --- GESTION DE PULSE HOLD (Independiente por canal) ---
            
            if hold_10ms_p > 0 then 
                hold_10ms_p <= hold_10ms_p - 1; 
                reg_10ms_p <= '1';
            else 
                reg_10ms_p <= '0'; 
            end if;
            
            if hold_10ms > 0 then 
                hold_10ms <= hold_10ms - 1; 
                reg_10ms <= '1'; 
            else 
                reg_10ms <= '0'; 
            end if;

            if hold_1ms  > 0 then 
                hold_1ms  <= hold_1ms  - 1; 
                reg_1ms  <= '1'; 
            else 
                reg_1ms  <= '0'; 
            end if;
            
            if hold_dave > 0 then 
                hold_dave <= hold_dave - 1; 
                reg_dave <= '1'; 
            else 
                reg_dave <= '0'; 
            end if;
            
            if hold_stress > 0 then 
                hold_stress <= hold_stress - 1; 
                reg_stress <= '1'; 
            else 
                reg_stress <= '0'; 
            end if;

            -- --- LOGICA DEL LED ---
            if blink_counter < (MAX_BLINK - 1) then
                blink_counter <= blink_counter + 1;
            else
                blink_counter <= 0;
                led_state <= not led_state;
            end if;
            
        end if;
    end process;

    -- Asignacion de salidas
    irq_out(0) <= reg_10ms;
    irq_out(1) <= reg_1ms;
    irq_out(2) <= reg_dave;
    irq_out(3) <= reg_stress;
    irq_out(4) <= reg_10ms_p; -- Repetimos Dave para pruebas de broadcast
    irq_out(5) <= reg_10ms_p; -- Repetimos Baseline
    
    led_out <= led_state;

end Behavioral;

