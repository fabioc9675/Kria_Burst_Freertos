library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity pulse_irq is
    Port ( 
        clk      : in  STD_LOGIC;  -- 100 MHz
        reset_n  : in  STD_LOGIC;
        irq_out  : out STD_LOGIC_VECTOR(5 downto 0);
        led_out  : out STD_LOGIC   -- Conecta esto a un pin de LED
    );
end pulse_irq;

architecture Behavioral of pulse_irq is
    constant MAX_IRQ   : integer := 1000000; -- 10 mili segundos
    constant MAX_BLINK : integer := 50000000;
    
    signal irq_counter   : integer range 0 to MAX_IRQ := 0;
    signal blink_counter : integer range 0 to MAX_BLINK := 0;
    signal led_state     : std_logic := '0';
    
    -- Señal auxiliar para controlar la duración del pulso
    signal irq_pulse_reg : std_logic := '0';
    signal pulse_hold    : integer range 0 to 15 := 0; 
begin

    process(clk, reset_n)
    begin
        if reset_n = '0' then
            irq_counter   <= 0;
            blink_counter <= 0;
            irq_pulse_reg <= '0';
            led_state     <= '0';
            pulse_hold    <= 0;
        elsif rising_edge(clk) then
            
            -- Lógica de la Interrupción mejorada
            if irq_counter < (MAX_IRQ - 1) then
                irq_counter <= irq_counter + 1;
            else
                irq_counter <= 0;
                irq_pulse_reg <= '1'; -- Iniciamos el pulso
                pulse_hold    <= 10;  -- Lo mantendremos 10 ciclos
            end if;

            -- Control de duración del pulso (Hold)
            if pulse_hold > 0 then
                pulse_hold    <= pulse_hold - 1;
                irq_pulse_reg <= '1';
            else
                irq_pulse_reg <= '0';
            end if;

            -- Lógica del LED (igual)
            if blink_counter < (MAX_BLINK - 1) then
                blink_counter <= blink_counter + 1;
            else
                blink_counter <= 0;
                led_state     <= not led_state;
            end if;
        end if;
    end process;

    irq_out(0) <= irq_pulse_reg;
    irq_out(1) <= irq_pulse_reg;
    irq_out(2) <= irq_pulse_reg;
    irq_out(3) <= irq_pulse_reg;
    irq_out(4) <= irq_pulse_reg;
    irq_out(5) <= irq_pulse_reg;
    led_out <= led_state;

end Behavioral;