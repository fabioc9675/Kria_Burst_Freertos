library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity bram_reader is
    generic (
            ADDR_SIZE : integer := 15
        );
    Port ( 
        clk         : in  STD_LOGIC;
        reset       : in  STD_LOGIC; -- Reset asincrono (activo en '0' segun tu estilo)
        -- Interfaz hacia la BRAM (Port B)
        bram_addr   : out STD_LOGIC_VECTOR(ADDR_SIZE-1 downto 0);
        bram_din    : in  STD_LOGIC_VECTOR(31 downto 0);
        bram_en     : out STD_LOGIC;
        bram_we     : out STD_LOGIC;
        -- Salida de diagnostico
        valid_led  : out STD_LOGIC
    );
end bram_reader;

architecture Behavioral of bram_reader is
    -- Constantes de validacion
    constant TARGET_ADDR : unsigned(ADDR_SIZE-1 downto 0) := B"000" & x"064"; -- Direcci0n 100 decimal
    constant TARGET_DATA : std_logic_vector(31 downto 0) := x"0000007B"; -- 123 decimal
begin

    bram_en <= '1'; -- Siempre habilitada para lectura continua
    bram_we <= '0'; -- Modo lectura


    process(clk, reset)
    begin
        if reset = '0' then
            valid_led <= '0';
            bram_addr <= (others => '0');
        elsif rising_edge(clk) then
            -- Apuntamos permanentemente a la direccion 100
            bram_addr <= std_logic_vector(TARGET_ADDR);
            
            -- Verificacion del dato leido
            if bram_din = TARGET_DATA then
                valid_led <= '1'; -- Enciende si el PS escribio 123 en la dir 100
            else
                valid_led <= '0';
            end if;
        end if;
    end process;

end Behavioral;