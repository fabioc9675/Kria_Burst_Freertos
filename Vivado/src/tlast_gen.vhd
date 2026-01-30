library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity axis_packet_controller is
    Generic (
        MAX_SAMPLES : integer := 48000
    );
    Port (
        clk             : in  STD_LOGIC;
        resetn          : in  STD_LOGIC;
        
        -- Interfaz de entrada (Viene de tu DDS/Lógica)
        s_axis_tdata    : in  STD_LOGIC_VECTOR(15 downto 0);
        s_axis_tvalid   : in  STD_LOGIC;
        s_axis_tready   : out STD_LOGIC;
        
        -- Interfaz de salida (Va al AXI DMA S2MM)
        m_axis_tdata    : out STD_LOGIC_VECTOR(31 downto 0);
        m_axis_tvalid   : out STD_LOGIC;
        m_axis_tready   : in  STD_LOGIC;
        m_axis_tlast    : out STD_LOGIC
    );
end axis_packet_controller;

architecture Behavioral of axis_packet_controller is
    signal sample_count : integer range 0 to MAX_SAMPLES := 0;
    signal can_transfer : STD_LOGIC;
begin

    can_transfer <= s_axis_tvalid and m_axis_tready;

    -- Pasamos los datos directamente con zero-padding
    m_axis_tdata  <= X"0000" & s_axis_tdata;
    m_axis_tvalid <= s_axis_tvalid;
    s_axis_tready <= m_axis_tready;

    -- TLAST debe estar presente EXACTAMENTE cuando se transfiere la última muestra
    -- Lo hacemos combinacional para que no tenga latencia respecto al contador
    m_axis_tlast <= '1' when (sample_count = MAX_SAMPLES - 1) else '0';

    process(clk)
    begin
        if rising_edge(clk) then
            if resetn = '0' then
                sample_count <= 0;
            else
                if can_transfer = '1' then
                    if sample_count = (MAX_SAMPLES - 1) then
                        sample_count <= 0; -- Reinicio tras la última muestra
                    else
                        sample_count <= sample_count + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;

end Behavioral;