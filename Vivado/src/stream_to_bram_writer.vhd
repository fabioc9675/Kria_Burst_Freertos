library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity stream_to_bram_writer is
    generic (
        ADDR_WIDTH  : integer := 32;   -- 2^16 = 65,536 (cubre las 48k muestras)
        MAX_SAMPLES : integer := 48000
    );
    port (
        clk           : in  std_logic;
        rst_n         : in  std_logic; -- Reset activo en bajo

        -- Interfaz AXI-Stream (DDS)
        s_axis_tdata  : in  std_logic_vector(15 downto 0);
        s_axis_tvalid : in  std_logic;
        s_axis_tready : out std_logic;

        -- Interfaz BRAM Puerto B (Native)
        bram_addr     : out std_logic_vector(ADDR_WIDTH-1 downto 0);
        bram_din      : out std_logic_vector(31 downto 0);
        bram_en       : out std_logic;
        bram_we       : out std_logic; -- 2 bits para 16-bit word

        -- Control y Estado
        start_capture : in  std_logic; -- Pulso desde GPIO
        done          : out std_logic  -- Hacia GPIO
    );
end stream_to_bram_writer;

architecture Behavioral of stream_to_bram_writer is
    signal addr_reg : unsigned(ADDR_WIDTH-1 downto 0) := (others => '0');
    signal writing  : std_logic := '0';
    signal done_reg : std_logic := '0';
begin

    -- Control de TREADY: listo si estamos capturando y no hemos terminado
    s_axis_tready <= writing and (not done_reg);
    
    -- Mapeo de salidas BRAM
    bram_addr <= std_logic_vector(addr_reg);
    bram_din  <= X"0000" & s_axis_tdata;
    bram_en   <= writing;
    
    -- WE activo solo cuando hay datos válidos y estamos en proceso de escritura
    bram_we   <= '1' when (writing = '1' and s_axis_tvalid = '1') else '0';
    
    done <= done_reg;

    process(clk)
    begin
        if rising_edge(clk) then
            if rst_n = '0' then
                addr_reg <= (others => '0');
                writing  <= '0';
                done_reg <= '0';
            else
                -- Iniciar captura al recibir pulso
                if start_capture = '1' then
                    addr_reg <= (others => '0');
                    writing  <= '1';
                    done_reg <= '0';
                end if;

                -- Lógica de conteo y guardado
                if writing = '1' and s_axis_tvalid = '1' then
                    if addr_reg = to_unsigned(MAX_SAMPLES - 1, ADDR_WIDTH) then
                        writing  <= '0';
                        done_reg <= '1'; -- Fin de la rafaga de 10ms
                    else
                        addr_reg <= addr_reg + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;

end Behavioral;