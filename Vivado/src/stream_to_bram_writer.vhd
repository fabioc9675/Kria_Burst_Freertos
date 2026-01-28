library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity stream_to_bram_writer is
    generic (
        ADDR_WIDTH  : integer := 16;   -- 2^16 cubre de sobra los 48k
        MAX_SAMPLES : integer := 48000
    );
    port (
        clk           : in  std_logic;
        rst_n         : in  std_logic; 
        s_axis_tdata  : in  std_logic_vector(15 downto 0);
        s_axis_tvalid : in  std_logic;
        s_axis_tready : out std_logic;

        -- Interfaz BRAM Puerto B (Native para el BRAM CTRL)
        bram_addr     : out std_logic_vector(ADDR_WIDTH-1 downto 0);
        bram_din      : out std_logic_vector(31 downto 0);
        bram_en       : out std_logic;
        bram_we       : out std_logic_vector(3 downto 0); -- Modo 32 bits

        start_capture : in  std_logic; 
        done          : out std_logic  
    );
end stream_to_bram_writer;

architecture Behavioral of stream_to_bram_writer is
    signal con_bram : integer range 0 to MAX_SAMPLES := 0;
    signal writing  : std_logic := '0';
begin

    bram_en   <= '1'; 
    bram_addr <= std_logic_vector(to_unsigned(con_bram, ADDR_WIDTH));
    
    -- Como el bus es de 32 bits, ponemos los 16 del DDS en la parte baja
    bram_din  <= x"0000" & s_axis_tdata; 
    
    -- El DDS fluye si estamos habilitados
    s_axis_tready <= writing;

    process(clk)
    begin
        if rising_edge(clk) then
            if rst_n = '0' then
                con_bram <= 0;
                writing  <= '0';
                done     <= '0';
                bram_we  <= (others => '0');
            else
                -- Lógica de inicio (Pulso desde el PS)
                if start_capture = '1' then
                    con_bram <= 0;
                    writing  <= '1';
                    done     <= '0';
                end if;

                if writing = '1' then
                    if s_axis_tvalid = '1' then
                        -- Escribimos en la RAM
                        bram_we <= (others => '1'); 
                        
                        if con_bram < MAX_SAMPLES - 1 then
                            con_bram <= con_bram + 1;
                        else
                            -- Terminamos la ráfaga
                            writing <= '0';
                            done    <= '1';
                            bram_we <= (others => '0');
                        end if;
                    else
                        -- Esperamos a que el DDS tenga dato válido
                        bram_we <= (others => '0');
                    end if;
                else
                    bram_we <= (others => '0');
                end if;
            end if;
        end if;
    end process;

end Behavioral;