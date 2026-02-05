library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity pl_monitor is
    Port (
        clk           : in  STD_LOGIC;
        resetn        : in  STD_LOGIC;
        -- AXI-Stream que viene del DMA (Canal de Lectura / MM2S)
        s_axis_tdata  : in  STD_LOGIC_VECTOR(31 downto 0);
        s_axis_tvalid : in  STD_LOGIC;
        s_axis_tready : out STD_LOGIC;
        s_axis_tlast  : in  STD_LOGIC;
        -- Salida al LED
        led_pin       : out STD_LOGIC;
        counter_out   : out STD_LOGIC_VECTOR(15 downto 0)
    );
end pl_monitor;

architecture Behavioral of pl_monitor is
    signal counter : integer := 0;
begin
    s_axis_tready <= '1'; -- Siempre aceptamos datos
    
    counter_out <= std_logic_vector(to_unsigned(counter, 16));

    process(clk)
    begin
        if rising_edge(clk) then
            if resetn = '0' then
                counter <= 0;
                led_pin <= '0';
            elsif s_axis_tvalid = '1' then
                -- Verificamos la posición 100
                if counter = 100 then
                    if s_axis_tdata = std_logic_vector(to_unsigned(123, 32)) then
                        led_pin <= '1'; -- Se queda encendido si lo encuentra
                    else
                        led_pin <= '0'; -- Se apaga si en la posición 100 no hay un 123
                    end if;
                end if;

                if s_axis_tlast = '1' then
                    counter <= 0;
                else
                    counter <= counter + 1;
                end if;
            end if;
        end if;
    end process;
end Behavioral;